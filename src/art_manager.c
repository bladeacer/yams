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

// In art_manager.c
void update_and_render_art(const char* src, const char* sig, int w, int h, bool force) {
    if (!src || strlen(src) < 2) return;

    long now = get_ms();
    bool changed = (strcmp(src, last_art_path) != 0) || (strcmp(sig, last_title_sig) != 0);

    if (changed) {
        if (art_pid > 0) {
            kill(art_pid, SIGKILL);
            waitpid(art_pid, NULL, WNOHANG);
            art_pid = -1;
        }
        strncpy(last_art_path, src, sizeof(last_art_path)-1);
        strncpy(last_title_sig, sig, sizeof(last_title_sig)-1);
        last_metadata_change_time = now;
    }

    // Check if the process finished
    if (art_pid > 0 && waitpid(art_pid, NULL, WNOHANG) != 0) {
        art_pid = -1;
    }

    // Trigger render if debounce passed AND (it's a new track OR we are forcing a redraw)
    if (now - last_metadata_change_time >= ART_DEBOUNCE_MS) {
        if (force || art_pid == -1) {
            // Use coordinates relative to where you want the art (e.g., inside main_win at 1,1)
            // Screen coordinates: main_win is at 1,1, so art at screen 2,2
            art_pid = render_art(1, 1, w, h); 
        }
    }
}
