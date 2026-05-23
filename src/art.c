#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <chafa.h>
#include "stb_image_resize.h"
#include "trim.h"
#include "metadata.h"
#include "art.h"
#include "utils.h"
#include "debug.h"

#define FUZZ_10PCT 25

static int extract_frame(const char *url, uint8_t **out_pixels, int *out_w, int *out_h) {
    AVFormatContext *fmt_ctx = NULL;
    int ret;

    ret = avformat_open_input(&fmt_ctx, url, NULL, NULL);
    if (ret < 0) return -1;

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) { avformat_close_input(&fmt_ctx); return -1; }

    int stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            stream_idx = i;
            break;
        }
    }
    if (stream_idx < 0)
        stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (stream_idx < 0) { avformat_close_input(&fmt_ctx); return -1; }

    AVStream *stream = fmt_ctx->streams[stream_idx];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { avformat_close_input(&fmt_ctx); return -1; }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) { avformat_close_input(&fmt_ctx); return -1; }

    ret = avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (ret < 0) { avcodec_free_context(&dec_ctx); avformat_close_input(&fmt_ctx); return -1; }

    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) { avcodec_free_context(&dec_ctx); avformat_close_input(&fmt_ctx); return -1; }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        ret = avcodec_send_packet(dec_ctx, &stream->attached_pic);
    } else {
        int got_packet = 0;
        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == stream_idx) {
                ret = avcodec_send_packet(dec_ctx, pkt);
                av_packet_unref(pkt);
                if (ret >= 0) { got_packet = 1; break; }
            } else {
                av_packet_unref(pkt);
            }
        }
        if (!got_packet) ret = -1;
    }

    if (ret < 0) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret < 0) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    int w = frame->width;
    int h = frame->height;

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

    struct SwsContext *sws = sws_getContext(w, h, src_fmt, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) {
        sws_freeContext(sws);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    uint8_t *dst_slice[4] = {rgba, NULL, NULL, NULL};
    int dst_stride[4] = {w * 4, 0, 0, 0};
    sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, h, dst_slice, dst_stride);

    *out_pixels = rgba;
    *out_w = w;
    *out_h = h;

    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
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
            pixels[off]     = (uint16_t)pixels[off]     * a / 255;
            pixels[off + 1] = (uint16_t)pixels[off + 1] * a / 255;
            pixels[off + 2] = (uint16_t)pixels[off + 2] * a / 255;
        }
        pixels[off + 3] = 255;
    }
}

static void render_chafa_to_gs(uint8_t *pixels, int img_w, int img_h,
                                int x, int y, int w, int h,
                                ChafaSymbolTags tags, GString *out) {
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
    chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED,
                                 pixels, img_w, img_h, img_w * 4);

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
    chafa_canvas_config_set_geometry(config, cell_w, cell_h);
    chafa_canvas_config_set_preprocessing_enabled(config, FALSE);
    chafa_canvas_config_set_bg_color(config, 0x000000);
    chafa_canvas_config_set_pixel_mode(config, pixel_mode);

    ChafaCanvas *canvas = chafa_canvas_new(config);
    chafa_canvas_draw_all_pixels(canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED,
                                 pixels, img_w, img_h, img_w * 4);

    GString *gs = chafa_canvas_print(canvas, NULL);
    if (gs) {
        g_string_append(out, gs->str);
        g_string_free(gs, TRUE);
    }

    chafa_canvas_unref(canvas);
    chafa_canvas_config_unref(config);
}

char *render_art_to_buffer(int x, int y, int w, int h,
                            ChafaSymbolTags tags, ChafaPixelMode pixel_mode, int is_ascii) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return NULL;

    uint8_t *pixels = NULL;
    int img_w, img_h;

    if (extract_frame(input_url, &pixels, &img_w, &img_h) != 0)
        return NULL;

    composite_alpha_on_black(pixels, img_w, img_h);

    BBox bbox = find_content_bbox(pixels, img_w, img_h, FUZZ_10PCT);
    if (bbox.top > 0 || bbox.bottom < img_h - 1 ||
        bbox.left > 0 || bbox.right < img_w - 1) {
        uint8_t *cropped = crop_pixels(pixels, img_w, img_h, bbox, &img_w, &img_h);
        if (cropped) {
            free(pixels);
            pixels = cropped;
        }
    }

    GString *out = g_string_sized_new(4096);

    if (pixel_mode == CHAFA_PIXEL_MODE_SYMBOLS) {
        if (is_ascii) {
            int ascii_w = (int)(w * 1.5);
            if (ascii_w < 1) ascii_w = 1;
            int rh = h > 0 ? h : 1;

            uint8_t *resized = malloc((size_t)ascii_w * rh * 4);
            if (resized) {
                stbir_resize_uint8(pixels, img_w, img_h, 0,
                                   resized, ascii_w, rh, 0, 4);
                render_chafa_to_gs(resized, ascii_w, rh, x, y, w, rh, tags, out);
                free(resized);
            } else {
                render_chafa_to_gs(pixels, img_w, img_h, x, y, w, h, tags, out);
            }
        } else {
            render_chafa_to_gs(pixels, img_w, img_h, x, y, w, h, tags, out);
        }
    } else {
        int px_w = w * 10;
        int px_h = h * 24;
        if (px_w < 1) px_w = 1;
        if (px_h < 1) px_h = 1;

        float src_aspect = (float)img_w / (float)img_h;
        float dst_aspect = (float)px_w / (float)px_h;

        int rw, rh;
        if (src_aspect > dst_aspect) {
            rw = px_w;
            rh = (int)(px_w / src_aspect);
            if (rh < 1) rh = 1;
        } else {
            rh = px_h;
            rw = (int)(px_h * src_aspect);
            if (rw < 1) rw = 1;
        }

        uint8_t *resized = malloc((size_t)rw * rh * 4);
        if (resized) {
            stbir_resize_uint8(pixels, img_w, img_h, 0,
                               resized, rw, rh, 0, 4);
            free(pixels);
            pixels = resized;
            img_w = rw;
            img_h = rh;
        }

        char clear[512];
        int cw = w < 510 ? w : 510;
        memset(clear, ' ', cw);
        clear[cw] = '\0';
        for (int r = 0; r < h; r++)
            g_string_append_printf(out, "\x1b[%d;%dH%s", y + 1 + r, x + 1, clear);

        g_string_append_printf(out, "\x1b[%d;%dH", y + 1, x + 1);
        render_pixel_to_gs(pixels, img_w, img_h, w, h, pixel_mode, out);
    }

    free(pixels);
    return g_string_free(out, FALSE);
}
