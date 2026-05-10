#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include "art_manager.h"
#include "art.h"
#include "utils.h"
#include "metadata.h"

static char last_art_path[4096] = {0};
static char last_title_sig[2048] = {0};
static long last_metadata_change_time = 0;
static pid_t art_pid = -1;

#define ART_DEBOUNCE_MS 200

long get_last_metadata_change_time(void) {
    return last_metadata_change_time;
}

void reset_art() {
    last_art_path[0] = '\0';
    last_title_sig[0] = '\0';
    last_metadata_change_time = get_ms();
}

void update_and_render_art(const char* src, const char* sig, int w, int h, bool force) {
    if (!src || strlen(src) < 2) return;

    long now = get_ms();
    bool changed = (strcmp(src, last_art_path) != 0) || (strcmp(sig, last_title_sig) != 0);

    if (changed) {
        last_metadata_change_time = now;
    }

    // Check if previous art process finished
    if (art_pid > 0 && waitpid(art_pid, NULL, WNOHANG) != 0) {
        art_pid = -1;
    }

    if (force || changed || art_pid == -1) {
        // Debounce to prevent flickering on rapid metadata updates
        if (!force && (now - last_metadata_change_time < ART_DEBOUNCE_MS)) {
            return;
        }

        if (art_pid > 0) {
            kill(art_pid, SIGKILL);
            waitpid(art_pid, NULL, 0);
        }

        art_pid = render_art(3, 1, w, h);
        
        strncpy(last_art_path, src, sizeof(last_art_path) - 1);
        strncpy(last_title_sig, sig, sizeof(last_title_sig) - 1);
    }
}
