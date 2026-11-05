#include <chafa.h>
#include <dirent.h>
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "art.h"
#include "config.h"
#include "debug.h"
#include "metadata.h"
#include "stb_image_resize.h"
#include "subprocess.h"
#include "trim.h"
#include "utils.h"

#define FUZZ_10PCT 25

extern char **environ;

/* Terminal capabilities are queried once and reused. */
static ChafaTermInfo *art_term_info(void) {
  static ChafaTermInfo *term_info = NULL;
  static bool initialized = false;

  if (!initialized) {
    initialized = true;
    ChafaTermDb *db = chafa_term_db_get_default();
    if (db)
      term_info = chafa_term_db_detect(db, environ);
  }
  return term_info;
}

static ChafaPassthrough art_passthrough_for(ChafaPixelMode pixel_mode) {
  ChafaTermInfo *term_info = art_term_info();

  if (!term_info ||
      !chafa_term_info_get_is_pixel_passthrough_needed(term_info, pixel_mode))
    return CHAFA_PASSTHROUGH_NONE;

  return chafa_term_info_get_passthrough_type(term_info);
}

/* tmux discards passthrough sequences unless its current window allows them.
 * Enable the option on demand and restore the previous value on exit. */
static int tmux_passthrough_state = -1; /* -1 untried, 0 failed, 1 ready */
static char *tmux_passthrough_previous = NULL;
static bool tmux_passthrough_changed = false;

static bool tmux_run(char *const argv[], CommandOutput *out) {
  const char *tmux = getenv("TMUX");
  if (!tmux || !tmux[0])
    return false;

  CommandOutput local;
  CommandOutput *result = out ? out : &local;
  if (!capture_command(argv, result))
    return false;

  bool ok = result->exited && result->exit_status == 0;
  if (!out)
    command_output_free(result);
  return ok;
}

static char *trimmed_copy(const char *s) {
  if (!s)
    return NULL;

  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
    s++;

  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                     s[len - 1] == '\n' || s[len - 1] == '\r'))
    len--;

  char *copy = malloc(len + 1);
  if (!copy)
    return NULL;
  memcpy(copy, s, len);
  copy[len] = '\0';
  return copy;
}

static bool ensure_tmux_passthrough(void) {
  if (tmux_passthrough_state >= 0)
    return tmux_passthrough_state == 1;

  tmux_passthrough_state = 0;

  char *show_argv[] = {"tmux", "show-options", "-w", "-v",
                       "allow-passthrough", NULL};
  CommandOutput out;
  if (!tmux_run(show_argv, &out))
    return false;

  char *current = trimmed_copy(out.data);
  command_output_free(&out);
  /* An empty value means the option was previously unset. */
  if (current && !current[0]) {
    free(current);
    current = NULL;
  }

  if (current && (strcmp(current, "on") == 0 ||
                  strcmp(current, "all") == 0)) {
    free(current);
    tmux_passthrough_state = 1;
    return true;
  }

  char *set_argv[] = {"tmux", "set-option", "-w",
                      "allow-passthrough", "on", NULL};
  if (!tmux_run(set_argv, NULL)) {
    free(current);
    return false;
  }

  /* Remember the old value (NULL when unset) for art_mode_cleanup(). */
  tmux_passthrough_previous = current;
  tmux_passthrough_changed = true;
  tmux_passthrough_state = 1;
  return true;
}

bool art_mode_ready(ChafaPixelMode pixel_mode) {
  if (pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS)
    return true;
  if (pixel_mode < 0 || pixel_mode >= CHAFA_PIXEL_MODE_MAX)
    return false;

  ChafaTermInfo *term_info = art_term_info();
  if (!term_info ||
      !chafa_term_info_is_pixel_mode_supported(term_info, pixel_mode))
    return false;

  if (!chafa_term_info_get_is_pixel_passthrough_needed(term_info, pixel_mode))
    return true;

  switch (chafa_term_info_get_passthrough_type(term_info)) {
    case CHAFA_PASSTHROUGH_TMUX:
      return ensure_tmux_passthrough();
    case CHAFA_PASSTHROUGH_SCREEN:
      return true;
    default:
      return false;
  }
}

const char *art_mode_unsupported_hint(ChafaPixelMode pixel_mode) {
  ChafaTermInfo *term_info = art_term_info();

  if (pixel_mode == CHAFA_PIXEL_MODE_KITTY) {
    if (term_info &&
        chafa_term_info_is_pixel_mode_supported(term_info, pixel_mode) &&
        chafa_term_info_get_is_pixel_passthrough_needed(term_info,
                                                        pixel_mode) &&
        chafa_term_info_get_passthrough_type(term_info) ==
            CHAFA_PASSTHROUGH_TMUX)
      return "enable tmux allow-passthrough";
    return "terminal has no Kitty graphics support";
  }

  if (pixel_mode == CHAFA_PIXEL_MODE_SIXELS)
    return "terminal has no sixel support";

  return "terminal does not support this mode";
}

int art_fallback_mode(int desired_mode) {
  if (desired_mode < 0 || desired_mode >= ART_MODE_COUNT)
    return 0;
  if (art_mode_ready(art_modes[desired_mode].pixel_mode))
    return desired_mode;

  ChafaTermInfo *term_info = art_term_info();
  if (term_info) {
    ChafaPixelMode best = chafa_term_info_get_best_pixel_mode(term_info);
    if (best != CHAFA_PIXEL_MODE_SYMBOLS && art_mode_ready(best)) {
      for (int i = 0; i < ART_MODE_COUNT; i++) {
        if (art_modes[i].pixel_mode == best)
          return i;
      }
    }
  }

  return 0;
}

void art_mode_cleanup(void) {
  if (!tmux_passthrough_changed)
    return;

  if (tmux_passthrough_previous) {
    char *restore_argv[] = {"tmux", "set-option", "-w", "allow-passthrough",
                            tmux_passthrough_previous, NULL};
    tmux_run(restore_argv, NULL);
  } else {
    char *unset_argv[] = {"tmux", "set-option", "-w", "-u",
                          "allow-passthrough", NULL};
    tmux_run(unset_argv, NULL);
  }

  free(tmux_passthrough_previous);
  tmux_passthrough_previous = NULL;
  tmux_passthrough_changed = false;
}

static int extract_frame(const char *url, uint8_t **out_pixels, int *out_w,
                         int *out_h) {
  DBG("extract_frame: url=[%s]", url);

  AVFormatContext *fmt_ctx = NULL;
  AVCodecContext *dec_ctx = NULL;
  AVPacket *pkt = NULL;
  AVFrame *frame = NULL;
  struct SwsContext *sws = NULL;
  uint8_t *rgba = NULL;
  int ret = -1;

  if (avformat_open_input(&fmt_ctx, url, NULL, NULL) < 0) {
    DBG("  avformat_open_input FAILED for [%s]", url);
    goto cleanup;
  }

  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    DBG("  avformat_find_stream_info FAILED for [%s]", url);
    goto cleanup;
  }

  DBG("  opened [%s], %u streams", url, fmt_ctx->nb_streams);

  int stream_idx = -1;
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
    DBG("    stream[%u]: codec_type=%d, disposition=0x%x", i,
        fmt_ctx->streams[i]->codecpar->codec_type,
        fmt_ctx->streams[i]->disposition);
    if (fmt_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      stream_idx = i;
      DBG("    -> ATTACHED_PIC found at stream %u", i);
      break;
    }
  }
  if (stream_idx < 0)
    stream_idx =
        av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (stream_idx < 0) {
    DBG("  no ATTACHED_PIC or video stream found in [%s]", url);
    goto cleanup;
  }

  DBG("  using stream[%d]", stream_idx);

  AVStream *stream = fmt_ctx->streams[stream_idx];
  const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!decoder) {
    DBG("  no decoder for codec_id=%d", stream->codecpar->codec_id);
    goto cleanup;
  }

  dec_ctx = avcodec_alloc_context3(decoder);
  if (!dec_ctx)
    goto cleanup;

  if (avcodec_parameters_to_context(dec_ctx, stream->codecpar) < 0)
    goto cleanup;

  /* Decode a single still frame. Keep libavcodec single-threaded: the extra
   * worker threads add no value here and their teardown is needless surface
   * area when yams is already running chafa/glib worker threads. */
  dec_ctx->pkt_timebase = stream->time_base;
  dec_ctx->thread_count = 1;
  dec_ctx->thread_type = 0;

  if (avcodec_open2(dec_ctx, decoder, NULL) < 0)
    goto cleanup;

  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  if (!pkt || !frame)
    goto cleanup;

  if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
    DBG("  decoding attached_pic packet");
    ret = avcodec_send_packet(dec_ctx, &stream->attached_pic);
  } else {
    DBG("  reading video frames");
    int got_packet = 0;
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
      if (pkt->stream_index == stream_idx) {
        ret = avcodec_send_packet(dec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret >= 0) {
          got_packet = 1;
          break;
        }
      } else {
        av_packet_unref(pkt);
      }
    }
    if (!got_packet) {
      DBG("  no video packet found in stream");
      ret = -1;
    }
  }

  if (ret < 0) {
    DBG("  avcodec_send_packet FAILED (ret=%d)", ret);
    goto cleanup;
  }

  ret = avcodec_receive_frame(dec_ctx, frame);
  if (ret == AVERROR(EAGAIN)) {
    /* Some video streams need one more packet (or an explicit flush) before
     * they produce their first frame. */
    if (avcodec_send_packet(dec_ctx, NULL) >= 0)
      ret = avcodec_receive_frame(dec_ctx, frame);
  }
  if (ret < 0) {
    DBG("  avcodec_receive_frame FAILED (ret=%d)", ret);
    goto cleanup;
  }

  int w = frame->width;
  int h = frame->height;
  DBG("  decoded frame: %dx%d, format=%d", w, h, frame->format);

  /* Reject nonsensical dimensions before they reach sws/malloc arithmetic. */
  if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
    DBG("  refusing invalid frame dimensions %dx%d", w, h);
    goto cleanup;
  }

  enum AVPixelFormat src_fmt = frame->format;
  if (src_fmt == AV_PIX_FMT_YUVJ420P) {
    src_fmt = AV_PIX_FMT_YUV420P;
    frame->color_range = AVCOL_RANGE_JPEG;
  } else if (src_fmt == AV_PIX_FMT_YUVJ422P) {
    src_fmt = AV_PIX_FMT_YUV422P;
    frame->color_range = AVCOL_RANGE_JPEG;
  } else if (src_fmt == AV_PIX_FMT_YUVJ444P) {
    src_fmt = AV_PIX_FMT_YUV444P;
    frame->color_range = AVCOL_RANGE_JPEG;
  }

  sws = sws_getContext(w, h, src_fmt, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR,
                       NULL, NULL, NULL);
  if (!sws) {
    DBG("  sws_getContext FAILED");
    goto cleanup;
  }

  rgba = malloc((size_t)w * h * 4);
  if (!rgba) {
    DBG("  malloc FAILED for %dx%d rgba", w, h);
    goto cleanup;
  }

  uint8_t *dst_slice[4] = {rgba, NULL, NULL, NULL};
  int dst_stride[4] = {w * 4, 0, 0, 0};
  if (sws_scale(sws, (const uint8_t *const *)frame->data, frame->linesize, 0,
                h, dst_slice, dst_stride) < 0) {
    DBG("  sws_scale FAILED");
    goto cleanup;
  }

  *out_pixels = rgba;
  *out_w = w;
  *out_h = h;
  rgba = NULL;
  ret = 0;

cleanup:
  free(rgba);
  sws_freeContext(sws);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  avcodec_free_context(&dec_ctx);
  avformat_close_input(&fmt_ctx);
  return ret;
}

static void composite_alpha_on_black(uint8_t *pixels, int w, int h) {
  for (int i = 0; i < w * h; i++) {
    int off = i * 4;
    uint8_t a = pixels[off + 3];
    if (a == 0) {
      pixels[off] = 0;
      pixels[off + 1] = 0;
      pixels[off + 2] = 0;
    } else if (a < 255) {
      pixels[off] = (uint16_t)pixels[off] * a / 255;
      pixels[off + 1] = (uint16_t)pixels[off + 1] * a / 255;
      pixels[off + 2] = (uint16_t)pixels[off + 2] * a / 255;
    }
    pixels[off + 3] = 255;
  }
}

static void render_chafa_to_gs(uint8_t *pixels, int img_w, int img_h, int x,
                               int y, int w, int h, ChafaSymbolTags tags,
                               GString *out) {
  ChafaSymbolMap *symbol_map = chafa_symbol_map_new();

  if (tags == 0) {
    static const char jp2a_chars[] = "    ...',;:clodxkO0KXNWM";
    bool seen[256] = {0};
    for (const char *p = jp2a_chars; *p; p++) {
      unsigned char c = (unsigned char)*p;
      if (!seen[c]) {
        seen[c] = true;
        chafa_symbol_map_add_by_range(symbol_map, c, c);
      }
    }
  } else {
    chafa_symbol_map_add_by_tags(symbol_map, tags);
  }

  ChafaCanvasConfig *config = chafa_canvas_config_new();
  chafa_canvas_config_set_geometry(config, w, h);
  chafa_canvas_config_set_symbol_map(config, symbol_map);
  chafa_canvas_config_set_fg_only_enabled(config, TRUE);
  chafa_canvas_config_set_bg_color(config, 0x000000);
  chafa_canvas_config_set_preprocessing_enabled(config, FALSE);
  chafa_canvas_config_set_canvas_mode(config, CHAFA_CANVAS_MODE_INDEXED_256);
  chafa_canvas_config_set_optimizations(config, CHAFA_OPTIMIZATION_NONE);
  chafa_canvas_config_set_dither_mode(config, CHAFA_DITHER_MODE_ORDERED);
  chafa_canvas_config_set_dither_intensity(config, 0.3);

  ChafaCanvas *canvas = chafa_canvas_new(config);
  chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, pixels,
                               img_w, img_h, img_w * 4);

  GString *gs = chafa_canvas_print(canvas, NULL);

  if (!gs) {
    chafa_canvas_unref(canvas);
    chafa_canvas_config_unref(config);
    chafa_symbol_map_unref(symbol_map);
    return;
  }

  char clear[512];
  int cw = w < 510 ? w : 510;
  memset(clear, ' ', cw);
  clear[cw] = '\0';
  for (int r = 0; r < h; r++)
    g_string_append_printf(out, "\x1b[%d;%dH%s", y + 1 + r, x + 1, clear);

  g_string_append(out, "\x1b[s");
  int row = 0;
  char *line = gs->str;
  char *next;
  while ((next = strchr(line, '\n')) && row < h) {
    *next = '\0';
    g_string_append_printf(out, "\x1b[%d;%dH%s", y + 1 + row, x + 1, line);
    row++;
    line = next + 1;
  }
  if (*line && row < h)
    g_string_append_printf(out, "\x1b[%d;%dH%s", y + 1 + row, x + 1, line);

  g_string_append(out, "\x1b[u\x1b[0m");

  g_string_free(gs, TRUE);
  chafa_canvas_unref(canvas);
  chafa_canvas_config_unref(config);
  chafa_symbol_map_unref(symbol_map);
}

static void render_pixel_to_gs(uint8_t *pixels, int img_w, int img_h,
                               int cell_w, int cell_h,
                               ChafaPixelMode pixel_mode, GString *out) {
  ChafaCanvasConfig *config = chafa_canvas_config_new();
  chafa_canvas_config_set_cell_geometry(config, 10, 20);
  chafa_canvas_config_set_geometry(config, cell_w, cell_h);
  chafa_canvas_config_set_preprocessing_enabled(config, FALSE);
  chafa_canvas_config_set_bg_color(config, 0x000000);
  chafa_canvas_config_set_pixel_mode(config, pixel_mode);
  chafa_canvas_config_set_passthrough(config, art_passthrough_for(pixel_mode));

  ChafaFrame *frame = chafa_frame_new(pixels, CHAFA_PIXEL_RGBA8_UNASSOCIATED,
                                      img_w, img_h, img_w * 4);
  ChafaImage *image = chafa_image_new();
  chafa_image_set_frame(image, frame);
  chafa_frame_unref(frame);

  /* A per-process placement ID keeps image data from colliding with other
   * yams instances sharing the same terminal (e.g. tmux panes). */
  static int placement_id = 0;
  if (placement_id == 0)
    placement_id = 1 + (int)(getpid() % 255);

  ChafaPlacement *placement = chafa_placement_new(image, placement_id);
  chafa_image_unref(image);
  chafa_placement_set_tuck(placement, CHAFA_TUCK_FIT);
  chafa_placement_set_halign(placement, CHAFA_ALIGN_CENTER);
  chafa_placement_set_valign(placement, CHAFA_ALIGN_CENTER);

  ChafaCanvas *canvas = chafa_canvas_new(config);
  chafa_canvas_set_placement(canvas, placement);
  chafa_placement_unref(placement);

  chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, pixels,
                               img_w, img_h, img_w * 4);

  GString *gs = chafa_canvas_print(canvas, NULL);
  if (gs) {
    g_string_append(out, gs->str);
    g_string_free(gs, TRUE);
  }

  chafa_canvas_unref(canvas);
  chafa_canvas_config_unref(config);
}

static char *get_cache_dir(void) {
  const char *cache_base = getenv("XDG_CACHE_HOME");
  if (cache_base) {
    char *dir = NULL;
    if (xasprintf(&dir, "%s/yams/art", cache_base) >= 0)
      return dir;
  }
  const char *home = getenv("HOME");
  if (home) {
    char *dir = NULL;
    if (xasprintf(&dir, "%s/.cache/yams/art", home) >= 0)
      return dir;
  }
  return strdup("/tmp/yams-art-cache");
}

static void ensure_dir(const char *dir) {
  char *d = strdup(dir);
  if (!d)
    return;
  for (char *p = d + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(d, 0755);
      *p = '/';
    }
  }
  mkdir(d, 0755);
  free(d);
}

static unsigned long hash_str(const char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + (unsigned char)c;
  return hash;
}

static char *get_cached_path(const char *url) {
  char *cache_dir = get_cache_dir();
  if (!cache_dir)
    return NULL;
  ensure_dir(cache_dir);
  char *path = NULL;
  int ret = xasprintf(&path, "%s/%lx", cache_dir, hash_str(url));
  free(cache_dir);
  return ret >= 0 ? path : NULL;
}

static int cache_version = 0;

static bool download_to_cache(const char *url, const char *cache_path) {
  AVIOContext *io = NULL;
  if (avio_open(&io, url, AVIO_FLAG_READ) < 0)
    return false;

  /* Write to a sibling temp file and only rename after a clean EOF, so a
   * partial or failed download never becomes a permanent cache hit. */
  char *tmp_path = NULL;
  if (xasprintf(&tmp_path, "%s.part.%ld", cache_path, (long)getpid()) < 0) {
    avio_close(io);
    return false;
  }

  FILE *f = fopen(tmp_path, "wb");
  if (!f) {
    avio_close(io);
    free(tmp_path);
    return false;
  }

  uint8_t buf[8192];
  int n;
  size_t total = 0;
  bool ok = true;

  while ((n = avio_read(io, buf, sizeof(buf))) > 0) {
    if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
      ok = false;
      break;
    }
    total += (size_t)n;
  }

  /* A clean EOF is 0 or AVERROR_EOF; any other negative value is a read
   * error and must not be committed. */
  if (n < 0 && n != AVERROR_EOF)
    ok = false;
  if (total == 0)
    ok = false;
  if (fflush(f) != 0 || ferror(f))
    ok = false;

  if (fclose(f) != 0)
    ok = false;
  if (avio_close(io) < 0)
    ok = false;

  if (!ok) {
    unlink(tmp_path);
    free(tmp_path);
    return false;
  }

  if (rename(tmp_path, cache_path) != 0) {
    unlink(tmp_path);
    free(tmp_path);
    return false;
  }

  free(tmp_path);
  cache_version++;
  return true;
}

static char *format_size(double bytes) {
  char *s = NULL;
  int ret;
  if (bytes < 1024.0)
    ret = xasprintf(&s, "%.0f B", bytes);
  else if (bytes < 1024.0 * 1024.0)
    ret = xasprintf(&s, "%.1f KB", bytes / 1024.0);
  else
    ret = xasprintf(&s, "%.1f MB", bytes / (1024.0 * 1024.0));
  return ret >= 0 ? s : NULL;
}

char *get_cache_info(void) {
  static int last_version = -1;
  static char cached[128] = "";

  if (last_version == cache_version && cached[0])
    return strdup(cached);

  char *cache_dir = get_cache_dir();
  char *result = NULL;
  DIR *d = cache_dir ? opendir(cache_dir) : NULL;
  if (d) {
    struct dirent *entry;
    int count = 0;
    double total = 0.0;
    while ((entry = readdir(d)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      char *path = NULL;
      if (xasprintf(&path, "%s/%s", cache_dir, entry->d_name) < 0)
        continue;
      struct stat st;
      if (stat(path, &st) == 0) {
        total += (double)st.st_size;
        count++;
      }
      free(path);
    }
    closedir(d);
    char *size_str = format_size(total);
    if (xasprintf(&result, "Art cache: %s (%d file%s)",
                  size_str ? size_str : "?", count, count == 1 ? "" : "s") < 0)
      result = NULL;
    free(size_str);
  } else {
    if (xasprintf(&result, "Art cache: empty") < 0)
      result = NULL;
  }
  free(cache_dir);

  if (result) {
    snprintf(cached, sizeof(cached), "%s", result);
    last_version = cache_version;
  } else {
    cached[0] = '\0';
  }
  return result;
}

char *clear_art_cache(void) {
  char *cache_dir = get_cache_dir();
  char *result = NULL;
  DIR *d = cache_dir ? opendir(cache_dir) : NULL;
  if (d) {
    struct dirent *entry;
    int removed = 0;
    double freed = 0.0;
    while ((entry = readdir(d)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      char *path = NULL;
      if (xasprintf(&path, "%s/%s", cache_dir, entry->d_name) < 0)
        continue;
      struct stat st;
      if (stat(path, &st) == 0)
        freed += (double)st.st_size;
      if (unlink(path) == 0)
        removed++;
      free(path);
    }
    closedir(d);
    char *size_str = format_size(freed);
    if (xasprintf(&result, "Cleared %s (%d file%s)",
                  size_str ? size_str : "?", removed,
                  removed == 1 ? "" : "s") < 0)
      result = NULL;
    free(size_str);
    cache_version++;
  } else {
    if (xasprintf(&result, "Cache directory not found") < 0)
      result = NULL;
  }
  free(cache_dir);
  return result;
}

void art_cache_bump(void) { cache_version++; }

const char *art_source_for_current_track(void) {
  if (strlen(m.art_url) > 5 && strncmp(m.art_url, "data:", 5) != 0) {
    /* A remote file_path copied into art_url by the data-URI/@ sentinel is a
     * track URL, not artwork. */
    if (strcmp(m.art_url, m.file_path) == 0 && is_remote_url(m.file_path))
      return NULL;
    return m.art_url;
  }

  /* A local file can carry attached art. A remote file_path is the track's
   * stream page, not an image, so fetching it only wastes time and retries. */
  if (strlen(m.file_path) > 5 && !is_remote_url(m.file_path))
    return m.file_path;

  return NULL;
}

/* Resolve an artwork source to a local path, downloading remote URLs into the
 * cache. *is_cache_path reports whether the returned path lives in the cache
 * (so callers can discard it if it does not decode); *was_hit reports whether
 * it was already there before this call. */
static char *resolve_cached_input(const char *input_url, bool *is_cache_path,
                                  bool *was_hit) {
  char *resolved = NULL;
  if (is_cache_path) *is_cache_path = false;
  if (was_hit) *was_hit = false;

  if (is_remote_url(input_url)) {
    char *cache_path = get_cached_path(input_url);
    if (cache_path) {
      struct stat st;
      if (stat(cache_path, &st) == 0 && st.st_size > 0) {
        DBG("  remote url, cache HIT: %s", cache_path);
        resolved = cache_path;
        if (was_hit) *was_hit = true;
      } else {
        /* Drop zero-length leftovers so they cannot pin an empty entry. */
        if (stat(cache_path, &st) == 0)
          unlink(cache_path);
        if (download_to_cache(input_url, cache_path)) {
          DBG("  remote url, downloaded to cache: %s", cache_path);
          resolved = cache_path;
        } else {
          free(cache_path);
          DBG("  remote url, download failed, using raw url");
        }
      }
      if (resolved && is_cache_path)
        *is_cache_path = true;
    }
  }

  if (!resolved)
    resolved = strdup(input_url);
  return resolved;
}

static uint8_t *decode_resolved_art(const char *resolved, int *out_w,
                                    int *out_h) {
  char decoded[4096];
  const char *av_path = resolved;
  if (strncmp(resolved, "file://", 7) == 0) {
    url_decode(decoded, resolved + 7);
    av_path = decoded;
    DBG("  decoded file:// path to [%s]", av_path);
  }

  DBG("  calling extract_frame with av_path=[%s]", av_path);

  uint8_t *pixels = NULL;
  int img_w = 0, img_h = 0;
  if (extract_frame(av_path, &pixels, &img_w, &img_h) != 0) {
    DBG("  extract_frame FAILED for [%s]", av_path);
    return NULL;
  }
  DBG("  extract_frame SUCCESS: %dx%d pixels for [%s]", img_w, img_h, av_path);
  *out_w = img_w;
  *out_h = img_h;
  return pixels;
}

static uint8_t *load_image(const char *input_url, int *out_w, int *out_h) {
  uint8_t *pixels = NULL;
  int img_w = 0, img_h = 0;
  bool is_cache_path = false;
  bool was_hit = false;

  char *resolved = resolve_cached_input(input_url, &is_cache_path, &was_hit);
  if (resolved) {
    pixels = decode_resolved_art(resolved, &img_w, &img_h);

    /* A cached body that does not decode (HTML error page, truncated legacy
     * entry, stale placeholder) must not remain a permanent cache hit. Drop
     * it and retry once from the network. */
    if (!pixels && is_cache_path && was_hit) {
      DBG("  cached art failed to decode, dropping [%s]", resolved);
      unlink(resolved);
      free(resolved);
      resolved = resolve_cached_input(input_url, &is_cache_path, &was_hit);
      if (resolved)
        pixels = decode_resolved_art(resolved, &img_w, &img_h);
    }

    /* A fresh download that still does not decode is not worth keeping. */
    if (!pixels && is_cache_path && resolved)
      unlink(resolved);
    free(resolved);
  }

  if (!pixels)
    return NULL;

  composite_alpha_on_black(pixels, img_w, img_h);

  BBox bbox = find_content_bbox(pixels, img_w, img_h, FUZZ_10PCT);
  if (bbox.top > 0 || bbox.bottom < img_h - 1 || bbox.left > 0 ||
      bbox.right < img_w - 1) {
    uint8_t *cropped = crop_pixels(pixels, img_w, img_h, bbox, &img_w, &img_h);
    if (cropped) {
      free(pixels);
      pixels = cropped;
    }
  }

  *out_w = img_w;
  *out_h = img_h;
  return pixels;
}

static void render_image(uint8_t *pixels, int img_w, int img_h, int x, int y,
                         int w, int h, ChafaSymbolTags tags,
                         ChafaPixelMode pixel_mode, int is_ascii, GString *out) {
  if (pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
    if (is_ascii) {
      int ascii_w = (int)(w * 1.5);
      if (ascii_w < 1)
        ascii_w = 1;
      int rh = h > 0 ? h : 1;

      double scale = fmin((double)ascii_w / img_w, (double)rh / img_h);
      int sw = (int)(img_w * scale);
      if (sw < 1)
        sw = 1;
      int sh = (int)(img_h * scale);
      if (sh < 1)
        sh = 1;

      uint8_t *resized = malloc((size_t)sw * sh * 4);
      if (resized) {
        stbir_resize_uint8(pixels, img_w, img_h, 0, resized, sw, sh, 0, 4);
        render_chafa_to_gs(resized, sw, sh, x, y, w, rh, tags, out);
        free(resized);
      } else {
        render_chafa_to_gs(pixels, img_w, img_h, x, y, w, h, tags, out);
      }
    } else {
      render_chafa_to_gs(pixels, img_w, img_h, x, y, w, h, tags, out);
    }
    return;
  }

  char clear[512];
  int cw = w < 510 ? w : 510;
  memset(clear, ' ', (size_t)cw);
  clear[cw] = '\0';
  for (int r = 0; r < h; r++)
    g_string_append_printf(out, "\x1b[%d;%dH%s", y + 1 + r, x + 1, clear);

  g_string_append_printf(out, "\x1b[%d;%dH", y + 1, x + 1);
  render_pixel_to_gs(pixels, img_w, img_h, w, h, pixel_mode, out);
}

char *render_art_to_buffer(int x, int y, int w, int h, ChafaSymbolTags tags,
                           ChafaPixelMode pixel_mode, int is_ascii) {
  DBG("render_art_to_buffer: art_url=[%s] (len=%zu), file_path=[%s] (len=%zu)",
      m.art_url, strlen(m.art_url), m.file_path, strlen(m.file_path));

  const char *input_url = art_source_for_current_track();
  DBG("  input_url selected: [%s] (len=%zu)", input_url ? input_url : "(null)",
      input_url ? strlen(input_url) : 0);

  if (!input_url) {
    DBG("  no usable art source, returning NULL");
    return NULL;
  }

  int img_w = 0, img_h = 0;
  uint8_t *pixels = load_image(input_url, &img_w, &img_h);
  if (!pixels)
    return NULL;

  GString *out = g_string_sized_new(4096);
  render_image(pixels, img_w, img_h, x, y, w, h, tags, pixel_mode, is_ascii,
               out);

  free(pixels);
  return g_string_free(out, FALSE);
}
