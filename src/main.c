#include <signal.h>
#include <fcntl.h>
#include <errno.h>
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
    DBG("handle_exit called with signal %d", sig);
    
    if (player_pipe) {
        int pipe_fd = fileno(player_pipe);
        close(pipe_fd); 
        
        // Option B: If you want to be clean, you'd need the PID 
        // to kill it, but for a simple UI, let's keep it robust:
        // pclose(player_pipe); // REMOVE OR COMMENT THIS OUT - it's the hang point
    }

    if (log_file) {
        DBG("Session ended. Closing log.");
        fclose(log_file);
    }

    cleanup_ui(sig); // This calls endwin() and exit()
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
    signal(SIGINT, handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    init_ui();
    DBG("UI Initialized. Entering Event Loop.");

    const char *fmt = "PLAYER|{{playerName}}¶STATUS|{{status}}¶TITLE|{{xesam:title}}¶"
                      "ARTIST|{{xesam:artist}}¶ALBUM|{{xesam:album}}¶PATH|{{xesam:url}}¶"
                      "LENGTH|{{mpris:length}}¶GENRE|{{xesam:genre}}¶COMPOSER|{{xesam:composer}}¶"
                      "ART|{{mpris:artUrl}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --follow --format '%s' 2>/dev/null", fmt);

    player_pipe = popen(cmd, "r");
    if (!player_pipe) {
        DBG("Failed to open playerctl pipe!");
        return 1;
    }

    int pipe_fd = fileno(player_pipe);
    char poll_buffer[BUFFER_SIZE];
    size_t buf_idx = 0;

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(pipe_fd, &fds);

        // Wait for activity
        int activity = select(pipe_fd + 1, &fds, NULL, NULL, NULL);

        if (activity < 0) {
            if (errno == EINTR) continue; // Handle signal interruptions (like window resize)
            DBG("Select error: %s", strerror(errno));
            break;
        }

        // --- HANDLE KEYBOARD ---
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int ch;
            // Drain ncurses input buffer
            while ((ch = getch()) != ERR) {
                DBG("Key pressed: %d ('%c')", ch, (ch >= 32 && ch <= 126) ? ch : '?');
                
                if (ch == 'q') {
                    DBG("Quit key detected.");
                    handle_exit(0);
                }
                else if (ch == 'r' || ch == KEY_RESIZE) {
                    DBG("Refresh/Resize triggered via key.");
                    resize_pending = true;
                }
                else if (ch == ' ') {
                    system("playerctl play-pause");
                }
                else if (ch == 'h' || ch == KEY_RIGHT) {
                    system("playerctl next");
                }
                else if (ch == 'l' || ch == KEY_LEFT) {
                    system("playerctl previous");
                }
            }
        }

        // --- HANDLE METADATA ---
        if (FD_ISSET(pipe_fd, &fds)) {
            char chunk[8192]; // Slightly larger read chunk
            ssize_t n = read(pipe_fd, chunk, sizeof(chunk) - 1);
            
            if (n <= 0) {
                DBG("Player pipe closed or error.");
                break; 
            }

            chunk[n] = '\0';
            
            if (buf_idx + n < BUFFER_SIZE) {
                memcpy(poll_buffer + buf_idx, chunk, n);
                buf_idx += n;
                poll_buffer[buf_idx] = '\0';

                if (strstr(poll_buffer, "[[END]]¶")) {
                    DBG("Complete metadata block received (%zu bytes).", buf_idx);
                    parse_block(poll_buffer);
                    draw_dashboard();
                    
                    memset(poll_buffer, 0, BUFFER_SIZE);
                    buf_idx = 0;
                }
            } else {
                DBG("Buffer overflow! Resetting buffer.");
                buf_idx = 0;
            }
        }

        if (resize_pending) {
            DBG("Processing pending resize.");
            draw_dashboard();
        }
        
        // Small sleep to prevent CPU spiking if select/getch desync
        usleep(1000); 
    }

    handle_exit(0);
    return 0;
}
