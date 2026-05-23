#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include "art_manager.h"
#include "art.h"
#include "utils.h"
#include "metadata.h"
#include "config.h"

static char last_art_path[4096] = {0};
static char last_title_sig[2048] = {0};
static long last_metadata_change_time = 0;
static pid_t art_pid = -1;
static bool render_pending = false;

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
    bool path_changed = (strcmp(src, last_art_path) != 0) || (strcmp(sig, last_title_sig) != 0);

    if (path_changed || force) {
        if (art_pid > 0) {
            kill(-art_pid, SIGKILL); 
            waitpid(art_pid, NULL, WNOHANG);
            art_pid = -1;
        }
        strncpy(last_art_path, src, sizeof(last_art_path)-1);
        strncpy(last_title_sig, sig, sizeof(last_title_sig)-1);
        last_metadata_change_time = now;
        render_pending = true;
    }

    int status;
    pid_t reaped;
    while ((reaped = waitpid(-1, &status, WNOHANG)) > 0) {
        if (reaped == art_pid) art_pid = -1;
    }

    bool timer_expired = (now - last_metadata_change_time >= get_art_debounce());
    
    if (render_pending && (force || timer_expired)) {
        if (art_pid == -1) {
            art_pid = render_art(2, 1, w, h); 
            render_pending = false; 
        }
    }
}
