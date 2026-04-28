#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ncurses.h>
#include <panel.h>
#include <locale.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

#define BUFFER_SIZE 262144
static char poll_buffer[BUFFER_SIZE];
static size_t buf_idx = 0;

FILE *log_file = NULL; 
int player_fd = -1;
long last_resize_time = 0;
static pid_t metadata_pid = -1;
static pid_t ctrl_pid = -1;

void handle_exit(int sig) {
    DBG("handle_exit called with signal %d", sig);
    
    if (player_fd != -1) close(player_fd);

    if (metadata_pid > 0) kill(metadata_pid, SIGKILL);
    if (ctrl_pid > 0) kill(ctrl_pid, SIGKILL);

    if (log_file) {
        DBG("Session ended. Closing log.");
        fclose(log_file);
    }

    cleanup_ui(sig); 
    exit(0);
}

void handle_winch(int sig) {
    (void)sig;
    resize_pending = true;
    last_resize_time = get_ms();
    resizeterm(0, 0); 
}

pid_t spawn_playerctl(char **args, int *out_fd) {
    int pipe_fds[2];
    if (out_fd && pipe(pipe_fds) == -1) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        
        if (out_fd) {
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDOUT_FILENO);
        } else {
            int devnull = open("/dev/null", O_WRONLY);
            dup2(devnull, STDOUT_FILENO);
        }
        
        int devnull_err = open("/dev/null", O_WRONLY);
        dup2(devnull_err, STDERR_FILENO);

        execvp("playerctl", args);
        _exit(1);
    }

    if (out_fd) {
        close(pipe_fds[1]);
        *out_fd = pipe_fds[0];
    }
    return pid;
}

void run_player_cmd(const char *action) {
    if (ctrl_pid > 0 && waitpid(ctrl_pid, NULL, WNOHANG) == 0) return;

    char *args[] = {"playerctl", (char *)action, NULL};
    ctrl_pid = spawn_playerctl(args, NULL);
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

void show_help_overlay() {
    static bool is_active = false;
    
    if (is_active) return;
    is_active = true;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int h = 14, w = 40;
    int start_y = (max_y - h) / 2;
    int start_x = (max_x - w) / 2;

    WINDOW *help_win = newwin(h, w, start_y, start_x);
    PANEL *help_panel = new_panel(help_win);
    
    wbkgd(help_win, COLOR_PAIR(3));
    box(help_win, 0, 0);
    
    wattron(help_win, A_BOLD | COLOR_PAIR(4));
    mvwprintw(help_win, 1, (w-10)/2, "YAMS HELP");
    wattroff(help_win, A_BOLD | COLOR_PAIR(4));
    mvwhline(help_win, 2, 1, ACS_HLINE, w-2);

    int ty = 4, tx = 4;
    #define HELP_L(k, d) { \
        wattron(help_win, COLOR_PAIR(4) | A_BOLD); mvwprintw(help_win, ty, tx, k); \
        wattroff(help_win, COLOR_PAIR(4) | A_BOLD); mvwprintw(help_win, ty++, tx + 6, d); \
    }

    HELP_L("q",     "Quit Application");
    HELP_L("r",     "Force Refresh UI");
    HELP_L("SPC",   "Play / Pause");
    HELP_L("h/<-",  "Previous Track");
    HELP_L("l/->",  "Next Track");
    HELP_L("?",     "Show this help");
    
    wattron(help_win, A_DIM);
    mvwprintw(help_win, h-2, tx, "Press any key to exit.");
    wattroff(help_win, A_DIM);

    update_panels();
    doupdate();

    nodelay(stdscr, FALSE);
    wgetch(help_win);
    nodelay(stdscr, TRUE);

    del_panel(help_panel);
    delwin(help_win);
    
    resize_pending = true;
    
    is_active = false;
}


static void handle_keypress(int ch) {
    if (ch == ERR) return;
    
    switch (ch) {
        case 'q':
            handle_exit(0);
            break;
        case ' ':
            run_player_cmd("play-pause");
            break;
        case 'h':
        case KEY_LEFT:
            run_player_cmd("previous");
            break;
        case 'l':
        case KEY_RIGHT:
            run_player_cmd("next");
            break;
        case 'r':
        case KEY_RESIZE:       
            resize_pending = true; 
            break;

        case 27:
        case KEY_MOUSE:
        case KEY_PPAGE:
        case KEY_NPAGE:
        case KEY_SR:
        case KEY_SF:
            break;
        case '?': 
            show_help_overlay();
            break;
        default:
            if (ch >= 32 && ch <= 126) {
                show_help_overlay();
            }
            break;
    }
}

int main() {
    setlocale(LC_ALL, "");
#ifndef NDEBUG
    log_file = fopen("yams.log", "a");
#endif

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

    char *meta_args[] = {"playerctl", "metadata", "--follow", "--format", (char*)fmt, NULL};
    metadata_pid = spawn_playerctl(meta_args, &player_fd);
    
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (player_fd != -1) FD_SET(player_fd, &fds);

        struct timeval tv = {0, 50000};
        int max_fd = (player_fd > STDIN_FILENO) ? player_fd : STDIN_FILENO;
        int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);

        if (activity < 0 && errno != EINTR) break;

        if (player_fd != -1 && FD_ISSET(player_fd, &fds)) {
            char chunk[8192];
            ssize_t n = read(player_fd, chunk, sizeof(chunk) - 1);
            if (n > 0) {
                chunk[n] = '\0';
                if (buf_idx + n < BUFFER_SIZE) {
                    memcpy(poll_buffer + buf_idx, chunk, n);
                    buf_idx += n;
                    poll_buffer[buf_idx] = '\0';
                    process_metadata_stream();
                }
            } else if (n <= 0) {
                close(player_fd);
                kill(metadata_pid, SIGKILL);
                waitpid(metadata_pid, NULL, 0);

                clear_metadata();
                draw_dashboard();

                metadata_pid = spawn_playerctl(meta_args, &player_fd);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            int ch;
            while ((ch = getch()) != ERR) handle_keypress(ch);
        }

        if (resize_pending) {
            draw_dashboard();
            resize_pending = false;
        }

        if (ctrl_pid > 0) waitpid(ctrl_pid, NULL, WNOHANG);
    }

    handle_exit(0);
    return 0;
}
