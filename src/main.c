#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <locale.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

FILE *log_file = NULL; 
long last_poll_time = 0;
FILE *player_pipe = NULL;
long last_resize_time = 0;
#define POLL_INTERVAL_MS 500  // Poll playerctl every 500ms
#define BUFFER_SIZE 262144

void handle_exit(int sig) {
    if (player_pipe) pclose(player_pipe);
    if (log_file) {
        DBG("Session ended via signal %d", sig);
        fclose(log_file);
    }
    cleanup_ui(sig);
}

void handle_winch(int sig) {
    (void)sig;
    resize_pending = true;
    last_resize_time = get_ms();
    resizeterm(0, 0); 
}

int main() {
    setlocale(LC_ALL, "");

    log_file = fopen("yams.log", "a");
    if (log_file) {
        fprintf(log_file, "\n--- SESSION START (POLLING MODE) ---\n");
        fflush(log_file);
    }

    DBG("CHECKPOINT: Starting Signal Setup");
    signal(SIGINT, handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    DBG("CHECKPOINT: Starting init_ui()");
    init_ui();
    DBG("CHECKPOINT: init_ui() Success");

    const char *fmt = "TITLE|{{xesam:title}}¶ARTIST|{{xesam:artist}}¶ALBUM|{{xesam:album}}¶PATH|{{xesam:url}}¶PLAYER|{{playerName}}¶STATUS|{{status}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --format '%s' 2>/dev/null", fmt);

    char poll_buffer[BUFFER_SIZE];
    DBG("CHECKPOINT: Entering Main Loop");
    while (1) {
        int ch = getch();
        bool should_draw = false;

        if (ch == 'q') break;

        // Trigger redraw on resize or scroll
        if (ch == KEY_RESIZE) should_draw = true;
        if (ch == 'j' || ch == KEY_DOWN) { scroll_pos++; should_draw = true; }
        if (ch == 'k' || ch == KEY_UP)   { if (scroll_pos > 0) scroll_pos--; should_draw = true; }

        // Inside the main while(1) loop:
        long now = get_ms();
        if (now - last_poll_time > POLL_INTERVAL_MS) {
            last_poll_time = now;

            FILE *fp = popen(cmd, "r");
            if (!fp) {
                DBG("UI_ERROR: popen failed for cmd: %s", cmd);
            } else {
                // Read the entire output (multiline)
                size_t read_bytes = fread(poll_buffer, 1, BUFFER_SIZE - 1, fp);
                if (read_bytes > 0) {
                    poll_buffer[read_bytes] = '\0';
                    parse_block(poll_buffer);
                    should_draw = true;
                } else {
                    DBG("POLL: No data received");
                    clear_metadata();
                    should_draw = true;
                }
                pclose(fp);
            }
        }

        if (should_draw) {
            draw_dashboard(); 
            if (resize_pending) {
                // Wait a tiny bit for the terminal to settle after resize
                usleep(50000); 
                resize_pending = false;
                draw_dashboard(); // Redraw once more with art
            }
        }

        usleep(10000); 
    }

    handle_exit(0); 
    return 0;
}
