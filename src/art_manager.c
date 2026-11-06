#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include "art_manager.h"
#include "art.h"
#include "utils.h"
#include "config.h"
#include "metadata.h"
#include "debug.h"

static char last_art_path[4096] = {0};
static char last_title_sig[2048] = {0};
static long last_metadata_change_time = 0;

static char *art_buffer = NULL;
static size_t art_buffer_len = 0;
/* Byte offset of the part that must be redrawn on every frame. Zero means the
 * whole buffer; art_buffer_len means nothing needs to be replayed. */
static size_t art_replay_offset = 0;
static bool art_full_replay = true;

/* Art is rendered with its top-left cell at these coordinates. */
static const int art_origin_x = 2;
static const int art_origin_y = 1;

static char retry_src[4096] = {0};
static int retry_attempt = 0;
static long retry_at = 0;

/* Separate budget for "the image rendered, but it is only a thumbnail"
 * refreshes. Failures and low-resolution retries share retry_src/retry_at so
 * the main loop only has one timer to watch. */
static int low_res_attempt = 0;
#define ART_LOW_RES_MAX_RETRIES 5

static const int retry_delays[] = {1000, 2000, 5000, 10000, 30000};

bool art_refresh_requested = false;

long get_last_metadata_change_time(void) {
    return last_metadata_change_time;
}

bool is_retry_pending(void) {
    return retry_src[0] != '\0';
}

long get_art_retry_at(void) {
    return retry_src[0] != '\0' ? retry_at : 0;
}

void expire_art_debounce(void) {
    last_metadata_change_time = 0;
}

void free_art(void) {
    free(art_buffer);
    art_buffer = NULL;
    art_buffer_len = 0;
    art_replay_offset = 0;
    art_full_replay = true;
}

void reset_art() {
    free(art_buffer);
    art_buffer = NULL;
    art_buffer_len = 0;
    art_replay_offset = 0;
    art_full_replay = true;
    last_art_path[0] = '\0';
    last_title_sig[0] = '\0';
    last_metadata_change_time = get_ms();
    retry_src[0] = '\0';
    retry_attempt = 0;
    retry_at = 0;
    low_res_attempt = 0;
    art_refresh_requested = true;
}

static void write_all(const char *data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = write(STDOUT_FILENO, data + offset, len - offset);
        if (n > 0) {
            offset += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
}

void replay_art_buffer(void) {
    if (!art_buffer || art_buffer_len == 0) return;

    size_t offset = art_full_replay ? 0 : art_replay_offset;
    if (offset >= art_buffer_len) return;

    /* A Kitty image body only needs to be sent once; the Unicode placeholder
     * grid that follows it is ordinary text, so it is replayed from the art
     * origin each frame. */
    if (offset > 0) {
        char pos[32];
        int n = snprintf(pos, sizeof(pos), "\x1b[%d;%dH",
                         art_origin_y + 1, art_origin_x + 1);
        if (n > 0)
            write_all(pos, (size_t)n);
    }

    write_all(art_buffer + offset, art_buffer_len - offset);
    art_full_replay = false;
}

static char *make_art_message(int x, int y, int w, int h, const char *msg) {
    char *buf = malloc(4096);
    if (!buf) return NULL;
    int pos = 0;
    char clear[128];
    int cw = w < 126 ? w : 126;
    if (cw < 0) cw = 0;
    memset(clear, ' ', (size_t)cw);
    clear[cw] = '\0';
    for (int r = 0; r < h && pos < 4090; r++)
        pos += snprintf(buf + pos, (size_t)(4096 - pos),
                        "\x1b[%d;%dH%s", y + 1 + r, x + 1, clear);
    if (msg && pos < 4095)
        snprintf(buf + pos, (size_t)(4096 - pos),
                 "\x1b[%d;%dH%s", y + h / 2, x + 2, msg);
    return buf;
}

/* Chafa's Kitty output consists of passthrough-wrapped image data followed by
 * a Unicode placeholder grid. Only the grid needs to be redrawn each frame.
 * Direct (non-passthrough) Kitty placements are stored by the terminal, so
 * there is nothing to replay at all. */
static void set_replay_offset_for(const ArtModeEntry *mode) {
    art_full_replay = true;
    art_replay_offset = 0;

    if (mode->pixel_mode != CHAFA_PIXEL_MODE_KITTY || !art_buffer)
        return;

    static const char marker[] = "\x1b\x1b\\\x1b\\";
    const size_t marker_len = sizeof(marker) - 1;
    const char *last = NULL;
    const char *p = art_buffer;

    while ((p = strstr(p, marker)) != NULL) {
        last = p;
        p += marker_len;
    }

    if (last) {
        size_t offset = (size_t)(last - art_buffer) + marker_len;
        /* Chafa may terminate the passthrough with an extra ST. */
        while (offset + 1 < art_buffer_len &&
               art_buffer[offset] == '\x1b' && art_buffer[offset + 1] == '\\')
            offset += 2;
        art_replay_offset = offset;
    } else {
        art_replay_offset = art_buffer_len;
    }
}

void update_and_render_art(const char* src, const char* sig, int w, int h, bool force) {
    DBG("update_and_render_art: src=[%s] sig=[%s] w=%d h=%d force=%d", src ? src : "(null)", sig ? sig : "(null)", w, h, force);
    if (!src)
        src = "";

    long now = get_ms();
    bool path_changed = (strcmp(src, last_art_path) != 0) || (strcmp(sig, last_title_sig) != 0);
    bool should_retry = false;

    DBG("  path_changed=%d (last_art_path=[%s], last_sig=[%s])", path_changed, last_art_path, last_title_sig);

    if (path_changed || force) {
        DBG("  clearing art buffer for new src");
        free(art_buffer);
        art_buffer = NULL;
        art_buffer_len = 0;
        strncpy(last_art_path, src, sizeof(last_art_path)-1);
        last_art_path[sizeof(last_art_path)-1] = '\0';
        strncpy(last_title_sig, sig, sizeof(last_title_sig)-1);
        last_title_sig[sizeof(last_title_sig)-1] = '\0';
        /* A new source (including "no artwork") must not inherit the previous
         * track's remote retry state. */
        retry_src[0] = '\0';
        retry_attempt = 0;
        retry_at = 0;
        low_res_attempt = 0;
        if (path_changed && is_remote_url(src)) {
            DBG("  setting retry for remote url: [%s]", src);
            strncpy(retry_src, src, sizeof(retry_src)-1);
            retry_src[sizeof(retry_src)-1] = '\0';
            retry_attempt = 0;
            retry_at = 0;
        }
    }

    if (art_buffer != NULL && retry_src[0] && strcmp(src, retry_src) == 0 && now >= retry_at) {
        DBG("  retry timer expired, should_retry=true");
        should_retry = true;
    }

    if (art_buffer != NULL && !should_retry) {
        DBG("  art_buffer already exists, skipping render");
        return;
    }

    /* The caller (render_new_art) already waits out the metadata debounce
     * before asking for a non-cached image. Re-checking it here meant the very
     * first render was always skipped, because reset_art() records the current
     * time immediately before the dashboard draws. An empty buffer is proof
     * that nothing has been rendered yet, so it must not be debounced. */
    bool timer_expired = (now - last_metadata_change_time >= get_art_debounce());
    bool first_render = (art_buffer == NULL);
    DBG("  timer_expired=%d elapsed=%ld debounce=%d first=%d", timer_expired,
        now - last_metadata_change_time, get_art_debounce(), first_render);

    if (force || timer_expired || should_retry || first_render) {
        DBG("  rendering new art");
        free(art_buffer);
        art_buffer = NULL;
        art_buffer_len = 0;

        const ArtModeEntry *mode = &art_modes[get_art_mode()];
        bool rendered = false;
        char *new_buf = NULL;

        if (art_mode_ready(mode->pixel_mode)) {
            new_buf = render_art_to_buffer_ex(art_origin_x, art_origin_y, w, h,
                                              mode->tags, mode->pixel_mode,
                                              mode->is_ascii, should_retry);
            rendered = (new_buf != NULL);
        } else {
            char msg[192];
            snprintf(msg, sizeof(msg), " Art mode '%s' unavailable: %s",
                     mode->name, art_mode_unsupported_hint(mode->pixel_mode));
            new_buf = make_art_message(art_origin_x, art_origin_y, w, h, msg);
        }

        if (new_buf) {
            DBG("  render_art_to_buffer SUCCESS, len=%zu", strlen(new_buf));
            art_buffer = new_buf;
            art_buffer_len = strlen(new_buf);
            if (rendered)
                set_replay_offset_for(mode);
            else {
                art_replay_offset = 0;
                art_full_replay = true;
            }
            retry_src[0] = '\0';
            retry_attempt = 0;

            /* A successfully rendered thumbnail is still worth refreshing: the
             * player may publish a larger art URL moments later, or the CDN
             * may serve a higher resolution copy on a fresh request. Keep the
             * small image on screen while retrying at the normal backoff. */
            if (rendered && is_remote_url(src) && art_last_image_is_low_res() &&
                low_res_attempt < ART_LOW_RES_MAX_RETRIES) {
                int idx = low_res_attempt < 5 ? low_res_attempt : 4;
                strncpy(retry_src, src, sizeof(retry_src) - 1);
                retry_src[sizeof(retry_src) - 1] = '\0';
                retry_at = now + retry_delays[idx];
                low_res_attempt++;
                DBG("  low-resolution art, scheduled refresh at %ld (attempt %d)",
                    retry_at, low_res_attempt);
            } else if (rendered) {
                low_res_attempt = 0;
            }
        } else {
            DBG("  render_art_to_buffer FAILED (returned NULL)");
            bool had_retry = (retry_src[0] != '\0');
            char msg[128];
            if (had_retry)
                snprintf(msg, sizeof(msg), " Retrying art... (attempt %d)",
                         retry_attempt + 1);
            else
                snprintf(msg, sizeof(msg), " No album art [%s]",
                         art_modes[get_art_mode()].name);
            new_buf = make_art_message(art_origin_x, art_origin_y, w, h, msg);
            if (new_buf) {
                art_buffer = new_buf;
                art_buffer_len = strlen(new_buf);
                art_replay_offset = 0;
                art_full_replay = true;
            }
            if (is_remote_url(src) && strcmp(src, retry_src) == 0) {
                int idx = retry_attempt < 5 ? retry_attempt : 4;
                retry_at = now + retry_delays[idx];
                retry_attempt++;
                DBG("  scheduled retry at %ld (attempt %d)", retry_at, retry_attempt);
            } else if (is_remote_url(src)) {
                strncpy(retry_src, src, sizeof(retry_src)-1);
                retry_src[sizeof(retry_src)-1] = '\0';
                retry_attempt = 1;
                retry_at = now + retry_delays[0];
                DBG("  first retry scheduled at %ld", retry_at);
            }
        }
    }
}
