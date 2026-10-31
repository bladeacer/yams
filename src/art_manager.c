#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
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

static char retry_src[4096] = {0};
static int retry_attempt = 0;
static long retry_at = 0;

static const int retry_delays[] = {1000, 2000, 5000, 10000, 30000};

bool art_refresh_requested = false;

long get_last_metadata_change_time(void) {
    return last_metadata_change_time;
}

bool is_retry_pending(void) {
    return retry_src[0] != '\0';
}

void expire_art_debounce(void) {
    last_metadata_change_time = 0;
}

void free_art(void) {
    free(art_buffer);
    art_buffer = NULL;
    art_buffer_len = 0;
}

void reset_art() {
    free(art_buffer);
    art_buffer = NULL;
    art_buffer_len = 0;
    last_art_path[0] = '\0';
    last_title_sig[0] = '\0';
    last_metadata_change_time = get_ms();
    retry_src[0] = '\0';
    retry_attempt = 0;
    retry_at = 0;
    art_refresh_requested = true;
}

void replay_art_buffer(void) {
    if (art_buffer && art_buffer_len > 0) {
        write(STDOUT_FILENO, art_buffer, art_buffer_len);
    }
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

void update_and_render_art(const char* src, const char* sig, int w, int h, bool force) {
    DBG("update_and_render_art: src=[%s] sig=[%s] w=%d h=%d force=%d", src ? src : "(null)", sig ? sig : "(null)", w, h, force);
    if (!src || strlen(src) < 2) {
        DBG("  src too short, returning");
        return;
    }

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

    bool timer_expired = (now - last_metadata_change_time >= get_art_debounce());
    DBG("  timer_expired=%d (elapsed=%ld, debounce=%d)", timer_expired, now - last_metadata_change_time, get_art_debounce());

    if (force || timer_expired || should_retry) {
        DBG("  rendering new art");
        free(art_buffer);
        art_buffer = NULL;
        art_buffer_len = 0;

        const ArtModeEntry *mode = &art_modes[get_art_mode()];
        char *new_buf = render_art_to_buffer(2, 1, w, h,
                                              mode->tags, mode->pixel_mode, mode->is_ascii);
        if (new_buf) {
            DBG("  render_art_to_buffer SUCCESS, len=%zu", strlen(new_buf));
            art_buffer = new_buf;
            art_buffer_len = strlen(new_buf);
            retry_src[0] = '\0';
            retry_attempt = 0;
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
            new_buf = make_art_message(2, 1, w, h, msg);
            if (new_buf) {
                art_buffer = new_buf;
                art_buffer_len = strlen(new_buf);
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
