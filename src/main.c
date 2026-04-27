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

    const char *fmt = "TITLE|{{xesam:title}}¶ARTIST|{{xesam:artist}}¶ALBUM|{{xesam:album}}¶PATH|{{xesam:url}}¶ART|{{mpris:artUrl}}¶PLAYER|{{playerName}}¶STATUS|{{status}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --format '%s' 2>/dev/null", fmt);

    char poll_buffer[BUFFER_SIZE];
    DBG("CHECKPOINT: Entering Main Loop");
    while (1) {

        int ch = getch();
        bool should_draw = false;

        if (ch != ERR) {
            switch(ch) {
                case 'q': cleanup_ui(0); break;
                case 'j': case KEY_DOWN:
                          scroll_pos++;
                          should_draw = true; 
                          break;
                case 'k': case KEY_UP:
                          scroll_pos--;
                          should_draw = true; 
                          break;
                case KEY_RESIZE:
                          resize_pending = true;
                          // Immediate resizeterm call helps ncurses update its internal maxyx
                          resizeterm(0, 0);
                          should_draw = true;
                          break;
            }
        }

        // Inside the main while(1) loop:
        long now = get_ms();
        if (now - last_poll_time > POLL_INTERVAL_MS) {
            last_poll_time = now;

            FILE *fp = popen(cmd, "r");
            if (fp) {
                memset(poll_buffer, 0, BUFFER_SIZE); // Clear before use
                size_t read_bytes = fread(poll_buffer, 1, BUFFER_SIZE - 1, fp);
                pclose(fp); // Close immediately after reading

                if (read_bytes > 0) {
                    parse_block(poll_buffer);
                    should_draw = true;
                }
            } else {
                DBG("UI_ERROR: popen failed for cmd: %s", cmd);
            }
        }

        if (should_draw) {
            draw_dashboard(); 
            if (resize_pending) {
                // Wait a tiny bit for the terminal to settle after resize
                usleep(30000); 
                resize_pending = false;
                draw_dashboard(); // Redraw once more with art
            }
        }

        usleep(10000); 
    }

    handle_exit(0); 
    return 0;
}
