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
#include <sys/ioctl.h>
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
static bool help_active = false;
static long last_player_fail_ms = 0;

void handle_exit(int sig) {
    DBG("handle_exit called with signal %d", sig);
    
    if (player_fd != -1) close(player_fd);

    if (metadata_pid > 0) kill(metadata_pid, SIGKILL);
    if (ctrl_pid > 0) kill(ctrl_pid, SIGKILL);

    if (log_file) {;
        DBG("Session ended. Closing log.");
        close_debug();
    }

    cleanup_ui(sig); 
    exit(0);
}

void handle_winch(int sig) {
    (void)sig;
    resize_pending = true;
    last_resize_time = get_ms();
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

void run_player_cmd(const char *action, const char *param) {
    if (ctrl_pid > 0 && waitpid(ctrl_pid, NULL, WNOHANG) == 0) return;

    char *args[4];
    args[0] = "playerctl";
    args[1] = (char *)action;
    args[2] = (char *)param;
    args[3] = NULL;

    ctrl_pid = spawn_playerctl(args, NULL);

    if (param)
        DBG("Running playerctl: %s %s", action, param);
    else
        DBG("Running playerctl: %s", action);
}

static void process_metadata_stream() {
    char *last_end = NULL;
    char *search_ptr = poll_buffer;

    while (true) {
        char *found = strstr(search_ptr, "[[END]]¶");
        if (!found) break;
        last_end = found;
        search_ptr = found + 8;
    }

    if (!last_end) return;

    size_t block_end_idx = (last_end + 8) - poll_buffer;

    char saved = poll_buffer[block_end_idx];
    poll_buffer[block_end_idx] = '\0';

    char *last_start = poll_buffer;
    char *current_pos = poll_buffer;
    while (current_pos < last_end) {
        char *found_start = strstr(current_pos, "PLAYER|");
        if (!found_start || found_start > last_end) break;
        last_start = found_start;
        current_pos = found_start + 7;
    }

    parse_block(last_start);
    resize_pending = true;

    poll_buffer[block_end_idx] = saved;
    size_t remaining = buf_idx - block_end_idx;
    if (remaining > 0) {
        memmove(poll_buffer, poll_buffer + block_end_idx, remaining);
    }
    buf_idx = remaining;
    poll_buffer[buf_idx] = '\0';
}

void show_seek_input() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int h = 3, w = 30;
    int sy = (max_y - h) / 2;
    int sx = (max_x - w) / 2;

    WINDOW *swin = newwin(h, w, sy, sx);
    PANEL *spanel = new_panel(swin);
    
    wbkgd(swin, COLOR_PAIR(3));
    box(swin, 0, 0);
    mvwprintw(swin, 0, 2, " Set Seek (sec) ");
    
    keypad(swin, TRUE);
    nodelay(swin, FALSE); 
    curs_set(1);
    
    update_panels();
    doupdate();

    char input[10] = {0};
    int idx = 0;
    const int field_x = 2;
    const int field_y = 1;

    while (true) {
        wmove(swin, field_y, field_x + idx);
        int ch = wgetch(swin);

        if (ch == 27) {
            idx = 0; 
            break; 
        }
        
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (idx > 0) {
                idx--;
                input[idx] = '\0';
                mvwaddch(swin, field_y, field_x + idx, ' ');
            }
        } 
        else if (ch >= '0' && ch <= '9' && idx < (int)sizeof(input) - 1) {
            input[idx] = ch;
            mvwaddch(swin, field_y, field_x + idx, ch);
            idx++;
            input[idx] = '\0';
        }
        
        wrefresh(swin);
    }

    curs_set(0);
    del_panel(spanel);
    delwin(swin);
    
    update_panels();
    doupdate();

    if (idx > 0) {
        int val = atoi(input);
        if (val > 0) set_seek_interval(val);
    }

    resize_pending = true;
}

void show_renderer_selection() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int h = 6, w = 35;
    int sy = (max_y - h) / 2;
    int sx = (max_x - w) / 2;

    WINDOW *awin = newwin(h, w, sy, sx);
    PANEL *apanel = new_panel(awin);
    
    wbkgd(awin, COLOR_PAIR(3));
    box(awin, 0, 0);
    mvwprintw(awin, 0, 2, " Select Art Renderer ");
    mvwprintw(awin, 1, 2, "[0] Chafa (Default)");
    mvwprintw(awin, 2, 2, "[1] jp2a (Native)");
    mvwprintw(awin, 4, 2, "Press any other key to cancel");
    mvwprintw(awin, 3, 2, "Choice: ");

    update_panels();
    doupdate();

    curs_set(1);
    int ch = mvwgetch(awin, 3, 10);
    curs_set(0);

    if (ch == '1') {
        set_preferred_renderer(1);
    } else if (ch == '0') {
        set_preferred_renderer(0);
    }
    // Any other key simply falls through and exits

    del_panel(apanel);
    delwin(awin);
    resize_pending = true;
}

static void handle_keypress(int ch);
void show_help_overlay(void);

static void handle_keypress(int ch) {
    if (ch == ERR) return;

    char seek_cmd[64];

    DBG("Keystroke: ID=%d Char='%c'", ch, (ch >= 32 && ch <= 126) ? ch : '?');

    switch (ch) {
        case 'q': handle_exit(0); break;
        case ' ': run_player_cmd("play-pause", NULL); break;
        case 'j':
            snprintf(seek_cmd, sizeof(seek_cmd), "%d-", get_seek_interval());
            run_player_cmd("position", seek_cmd);
            break;
        case 'k':
            snprintf(seek_cmd, sizeof(seek_cmd), "%d+", get_seek_interval());
            run_player_cmd("position", seek_cmd);
            break;
        case 'a':
            show_renderer_selection();
            reset_art();
            resize_pending = true;
            break;
        case '+':
            set_seek_interval(get_seek_interval() + 5);
            resize_pending = true;
            break;
        case '-':
            set_seek_interval(get_seek_interval() - 5);
            resize_pending = true;
            break;

        case 'h':
        case KEY_LEFT: run_player_cmd("previous", NULL); break;
        case 'l':
        case KEY_RIGHT: run_player_cmd("next", NULL); break;
        case 'r':
            DBG("Manual refresh requested");
            reset_art();
            resize_pending = true;
            last_resize_time = get_ms() - 1000;
            break;

        case 's':
            show_seek_input();
            break;

        case 27: case KEY_MOUSE: case KEY_PPAGE: case KEY_NPAGE:
        case KEY_SR: case KEY_SF: case 259: case 258: case 410:
            break;

        case '?': 
            if (!help_active) show_help_overlay(); 
            break;

        default:
            if (!help_active && ch >= 32 && ch <= 126) {
                show_help_overlay();
            }
            break;
    }
}

void show_help_overlay() {
    if (help_active) return;
    help_active = true;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int h = 16, w = 45;
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

    int ty = 4, tx = 6;
    #define HELP_L(k, d) { \
        wattron(help_win, COLOR_PAIR(4) | A_BOLD); mvwprintw(help_win, ty, tx, k); \
        wattroff(help_win, COLOR_PAIR(4) | A_BOLD); mvwprintw(help_win, ty++, tx + 8, d); \
    }

    HELP_L("q",     "Quit Application");
    HELP_L("r",     "Force Refresh UI");
    HELP_L("SPC",   "Play / Pause");
    HELP_L("h/<-",  "Previous Track");
    HELP_L("l/->",  "Next Track");
    HELP_L("j/k",   "Seek Interval");
    HELP_L("-/+",   "Adjust Seek Interval");
    HELP_L("s",     "Set Seek Interval");
    HELP_L("ESC/?", "Close this menu");

    wattron(help_win, A_DIM);
    mvwprintw(help_win, h-2, (w-26)/2, "Press ESC or ? to return");
    wattroff(help_win, A_DIM);

    update_panels();
    doupdate();

    int ch;
    nodelay(stdscr, FALSE);
    while (true) {
        ch = wgetch(help_win);
        if (resize_pending) {
            break;
        }

        if (ch == ERR) {
            usleep(10000); 
            continue;
        }
        
        if (ch == 27 || ch == '?' || ch == 'q') {
            if (ch == 'q') handle_exit(0);
            break;
        }

        handle_keypress(ch);
        
        update_panels();
        doupdate();
    }
    nodelay(stdscr, TRUE);

    del_panel(help_panel);
    delwin(help_win);
    
    resize_pending = true;
    help_active = false;
}

int main() {
    setlocale(LC_ALL, "");

    if (getenv("YAMS_EXIT_IMMEDIATELY")) {
        init_ui();
        cleanup_ui(0);
        return 0;
    }

    init_debug();

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

        int max_fd = STDIN_FILENO;

        if (ctrl_pid > 0) waitpid(ctrl_pid, NULL, WNOHANG);

        if (player_fd == -1 && (get_ms() - last_player_fail_ms > 2000)) {
            DBG("Attempting playerctl restart...");
            metadata_pid = spawn_playerctl(meta_args, &player_fd);
            last_player_fail_ms = get_ms(); 
        }

        if (player_fd != -1) {
            FD_SET(player_fd, &fds);
            if (player_fd > max_fd) max_fd = player_fd;
        }

        struct timeval tv = {0, 100000}; 
        
        int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);

        if (resize_pending) {
            long now = get_ms();
            if (now - last_resize_time > 200) {
                struct winsize size;
                if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
                    resizeterm(size.ws_row, size.ws_col);
                }

                draw_dashboard();
                resize_pending = false;
            }
        }

        if (activity < 0) {
            if (errno == EINTR) continue;
            break; 
        }

        if (activity > 0) {
            if (player_fd != -1 && FD_ISSET(player_fd, &fds)) {
                char chunk[8192];
                ssize_t n = read(player_fd, chunk, sizeof(chunk) - 1);
                
                if (n > 0) {
                    static bool skipping_art = false;
                    for (ssize_t i = 0; i < n; i++) {
                        if (!skipping_art && buf_idx >= 14) {
                            if (strncmp(poll_buffer + (buf_idx - 14), "ART|data:image", 14) == 0) {
                                skipping_art = true;
                                DBG("Massive ART detected: Dropping Base64 bytes...");
                            }
                        }

                        if (skipping_art) {
                            if ((unsigned char)chunk[i] == 0xC2 && i + 1 < n && (unsigned char)chunk[i+1] == 0xB6) {
                                skipping_art = false;
                                i++;
                                if (buf_idx + 2 < BUFFER_SIZE) {
                                    poll_buffer[buf_idx++] = (char)0xC2;
                                    poll_buffer[buf_idx++] = (char)0xB6;
                                }
                                DBG("ART skip finished. Resuming ingestion.");
                            }
                            continue;
                        }

                        if (buf_idx < BUFFER_SIZE - 1) {
                            poll_buffer[buf_idx++] = chunk[i];
                        } else {
                            DBG("Buffer overflow! Safety reset.");
                            buf_idx = 0;
                            break;
                        }
                    }
                    poll_buffer[buf_idx] = '\0';
                    process_metadata_stream();
                } 
                else if (n <= 0) {
                    close(player_fd);
                    player_fd = -1;

                    if (metadata_pid > 0) {
                        kill(metadata_pid, SIGKILL);
                        waitpid(metadata_pid, NULL, 0);
                        metadata_pid = -1;
                    }

                    clear_metadata();
                    resize_pending = true;

                    last_player_fail_ms = get_ms();
                    DBG("Player connection lost.");
                }
            }

            if (FD_ISSET(STDIN_FILENO, &fds)) {
                int ch = getch();
                if (ch == ERR) {
                    static int error_count = 0;
                    if (++error_count > 10) {
                        FD_CLR(STDIN_FILENO, &fds); 
                        struct timespec ts = {0, 10000000};
                        nanosleep(&ts, NULL);
                    }
                } else {
                    handle_keypress(ch);
                    while ((ch = getch()) != ERR) {
                        handle_keypress(ch);
                    }
                }
            }
        }

    }

    handle_exit(0);
    return 0;
}
