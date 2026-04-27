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
#define POLL_INTERVAL_MS 500  
#define BUFFER_SIZE 262144 // 256KB is plenty for text-first parsing

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

    signal(SIGINT, handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    init_ui();

    /* * IMPROVED FORMAT STRING:
     * We put PLAYER and STATUS first. If ART is a massive base64 string 
     * that exceeds our buffer, we will still have captured the 
     * critical identity of the track before the buffer truncates.
     */
    const char *fmt = "PLAYER|{{playerName}}¶"
                      "STATUS|{{status}}¶"
                      "TITLE|{{xesam:title}}¶"
                      "ARTIST|{{xesam:artist}}¶"
                      "ALBUM|{{xesam:album}}¶"
                      "PATH|{{xesam:url}}¶"
                      "LENGTH|{{mpris:length}}¶"
                      "GENRE|{{xesam:genre}}¶"
                      "COMPOSER|{{xesam:composer}}¶"
                      "ART|{{mpris:artUrl}}¶"
                      "LYRICS|{{xesam:asText}}¶[[END]]¶";

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --format '%s' 2>/dev/null", fmt);

    char poll_buffer[BUFFER_SIZE];
    
    while (1) {
        int ch = getch();
        bool should_draw = false;

        if (ch != ERR) {
            switch(ch) {
                case 'q': handle_exit(0); break;
                case 'j': case KEY_DOWN:
                          if (scroll_pos < m.lyric_count - 1) scroll_pos++;
                          should_draw = true; 
                          break;
                case 'k': case KEY_UP:
                          if (scroll_pos > 0) scroll_pos--;
                          should_draw = true; 
                          break;
                case 'r':
                case KEY_RESIZE:
                          resize_pending = true;
                          should_draw = true;
                          break;
            }
        }

        long now = get_ms();
        if (now - last_poll_time > POLL_INTERVAL_MS) {
            last_poll_time = now;

            FILE *fp = popen(cmd, "r");
            if (fp) {
                memset(poll_buffer, 0, BUFFER_SIZE);
                
                // 1. Read the first 256KB (the "Good Stuff")
                size_t read_bytes = fread(poll_buffer, 1, BUFFER_SIZE - 1, fp);
                
                // 2. GHOST BUSTER: Drain the remaining pipe.
                // If mpv sent a 2MB base64 string, we MUST read it all out
                // or the next popen() might get desynced/stale data.
                char junk[8192];
                while (fread(junk, 1, sizeof(junk), fp) > 0);
                
                pclose(fp);

                if (read_bytes > 0) {
                    parse_block(poll_buffer);
                } else {
                    // Only clear if playerctl actually fails/finds no players
                    clear_metadata();
                }
                should_draw = true; 
            }
        }

        if (should_draw) {
            draw_dashboard(); 
        }

        usleep(10000); 
    }

    return 0;
}
