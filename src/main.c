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
FILE *player_pipe = NULL;
long last_poll_time = 0;
long last_resize_time = 0;

#define BUFFER_SIZE 262144
static char poll_buffer[BUFFER_SIZE];
static size_t buf_idx = 0;

void handle_exit(int sig) {
    DBG("handle_exit called with signal %d", sig);
    
    if (player_pipe) {
        int pipe_fd = fileno(player_pipe);
        close(pipe_fd); 
    }

    if (log_file) {
        DBG("Session ended. Closing log.");
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

static void process_metadata_stream() {
    char *end_ptr;
    bool updated = false;

    while ((end_ptr = strstr(poll_buffer, "[[END]]¶")) != NULL) {
        size_t block_len = (end_ptr + 8) - poll_buffer;
        
        char saved = poll_buffer[block_len];
        poll_buffer[block_len] = '\0';
        
        parse_block(poll_buffer);
        updated = true;
        
        poll_buffer[block_len] = saved;

        size_t remaining = buf_idx - block_len;
        if (remaining > 0) {
            memmove(poll_buffer, poll_buffer + block_len, remaining);
        }
        buf_idx = remaining;
        poll_buffer[buf_idx] = '\0';
    }

    if (updated) draw_dashboard();
}

static void handle_keypress(int ch) {
    if (ch == ERR) return;
    
    const char *cmd = NULL;
    switch (ch) {
        case 'q':          handle_exit(0);                 break;
        case ' ':          cmd = "playerctl play-pause &"; break;
        case 'h':
        case KEY_RIGHT:    cmd = "playerctl next &";       break;
        case 'l':
        case KEY_LEFT:     cmd = "playerctl previous &";   break;
        case 'r':
        case KEY_RESIZE:       
            if (system("playerctl -l >/dev/null 2>&1") != 0) {
                clear_metadata();
            }
            resize_pending = true; 
            break;
    }

    if (cmd) {
        system(cmd);
        usleep(50000);
        resize_pending = true;
    }
}

int main() {
    setlocale(LC_ALL, "");
    log_file = fopen("yams.log", "a");
    
    signal(SIGINT,  handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    init_ui();
    draw_dashboard();

    DBG("UI Initialized. Entering Event Loop.");

    const char *fmt = "PLAYER|{{playerName}}¶STATUS|{{status}}¶TITLE|{{xesam:title}}¶"
                      "ARTIST|{{xesam:artist}}¶ALBUM|{{xesam:album}}¶PATH|{{xesam:url}}¶"
                      "LENGTH|{{mpris:length}}¶GENRE|{{xesam:genre}}¶COMPOSER|{{xesam:composer}}¶"
                      "ART|{{mpris:artUrl}}¶LYRICS|{{xesam:asText}}¶[[END]]¶";

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "playerctl metadata --follow --format '%s' 2>/dev/null", fmt);
    
    player_pipe = popen(cmd, "r");
    if (!player_pipe) return 1;
    
    int pipe_fd = fileno(player_pipe);

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(pipe_fd, &fds);

        struct timeval tv = {0, 50000};
        int activity = select(pipe_fd + 1, &fds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno == EINTR) {
                continue; 
            }
            break;
        }

        if (FD_ISSET(pipe_fd, &fds)) {
            char chunk[8192];
            ssize_t n = read(pipe_fd, chunk, sizeof(chunk) - 1);
            if (n > 0) {
                chunk[n] = '\0';
                if (buf_idx + n < BUFFER_SIZE) {
                    memcpy(poll_buffer + buf_idx, chunk, n);
                    buf_idx += n;
                    poll_buffer[buf_idx] = '\0';
                    process_metadata_stream();
                }
            } else if (n <= 0) {
                pclose(player_pipe);
                clear_metadata();
                draw_dashboard();
                player_pipe = popen(cmd, "r");
                pipe_fd = fileno(player_pipe);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int ch;
            while ((ch = getch()) != ERR) {
                handle_keypress(ch);
            }
        }

        if (resize_pending) {
            draw_dashboard();
            resize_pending = false;
        }
    }

    handle_exit(0);
    return 0;
}
