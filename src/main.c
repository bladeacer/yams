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
#include <sys/select.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"
#include "config.h"

#define BUFFER_SIZE 131072
static char poll_buffer[BUFFER_SIZE];
static size_t buf_idx = 0;

FILE *log_file = NULL; 
int player_fd = -1;
long last_resize_time = 0;
static pid_t metadata_pid = -1;
static pid_t ctrl_pid = -1;
static bool help_active = false;
static long last_player_fail_ms = 0;
long last_draw_time = 0;

void handle_exit(int sig) {
    DBG("handle_exit called with signal %d", sig);
    if (player_fd != -1) close(player_fd);
    if (metadata_pid > 0) kill(metadata_pid, SIGKILL);
    if (ctrl_pid > 0) kill(ctrl_pid, SIGKILL);
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
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);

        char *args[4] = {"playerctl", (char *)action, (char *)param, NULL};
        execvp("playerctl", args);
        _exit(1);
    } else if (pid > 0) {
        ctrl_pid = pid; 
    }
}

static bool process_metadata_stream() {
    bool changed = false;
    char *start = poll_buffer;
    
    while (1) {
        char *end_tag = strstr(start, "[[END]]");
        if (!end_tag) break;

        char *target = strstr(start, "PLAYER|");
        if (target && target < end_tag) {
            *end_tag = '\0';
            if (parse_block(target)) {
                changed = true;
            }
        }

        start = end_tag + 7; 
    }

    size_t processed_len = start - poll_buffer;
    if (processed_len > 0) {
        size_t remaining = buf_idx - processed_len;
        if (remaining > 0) {
            memmove(poll_buffer, poll_buffer + processed_len, remaining);
        }
        buf_idx = remaining;
        poll_buffer[buf_idx] = '\0';
    }
    
    return changed;
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

void show_braille_toggle() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int h = 6, w = 40;
    int sy = (max_y - h) / 2;
    int sx = (max_x - w) / 2;

    WINDOW *awin = newwin(h, w, sy, sx);
    PANEL *apanel = new_panel(awin);
    
    wbkgd(awin, COLOR_PAIR(3));
    box(awin, 0, 0);
    int current = get_braille_mode();
    mvwprintw(awin, 0, 2, " Art Mode ");
    mvwprintw(awin, 1, 2, "[b] Toggle braille mode");
    mvwprintw(awin, 2, 2, "Current: %s", current ? "braille" : "ascii");
    mvwprintw(awin, 4, 2, "Press any other key to cancel");

    update_panels();
    doupdate();

    curs_set(1);
    int ch = mvwgetch(awin, 3, 2);
    curs_set(0);

    if (ch == 'b' || ch == 'B')
        set_braille_mode(!current);

    del_panel(apanel);
    delwin(awin);
    resize_pending = true;
}

static void handle_keypress(int ch, long now);
void show_help_overlay(long now);

static void handle_keypress(int ch, long now) {
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
            show_braille_toggle();
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
        case KEY_LEFT:
            run_player_cmd("previous", NULL);
            last_draw_time = 0;
            break;
        case 'l':
        case KEY_RIGHT:
            run_player_cmd("next", NULL);
            last_draw_time = 0;
            break;
        case 'r':
            DBG("Manual refresh requested");
            reset_art();
            resize_pending = true;
            last_resize_time = now - 1000;
            break;

        case 's':
            show_seek_input();
            break;

        case 27: case KEY_MOUSE: case KEY_PPAGE: case KEY_NPAGE:
        case KEY_SR: case KEY_SF: case 259: case 258: case 410:
            break;

        case '?': 
            if (!help_active) show_help_overlay(now); 
            break;

        default:
            if (!help_active && ch >= 32 && ch <= 126) {
                show_help_overlay(now);
            }
            break;
    }
}

void show_help_overlay(long now) {
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
        fd_set help_fds;
        FD_ZERO(&help_fds);
        FD_SET(STDIN_FILENO, &help_fds);
        struct timeval h_tv = {0, 250000};

        int h_act = select(STDIN_FILENO + 1, &help_fds, NULL, NULL, &h_tv);

        if (h_act > 0) {
            ch = wgetch(help_win);
            if (ch == 27 || ch == '?' || ch == 'q') break;
            handle_keypress(ch, now);
            update_panels();
            doupdate();
        }
    }
    nodelay(stdscr, TRUE);

    del_panel(help_panel);
    delwin(help_win);
    
    resize_pending = true;
    help_active = false;
}

static void handle_player_data() {
    char chunk[16384];
    ssize_t n;
    bool data_read = false;

    while ((n = read(player_fd, chunk, sizeof(chunk))) > 0) {
        if (buf_idx + n < BUFFER_SIZE - 1) {
            memcpy(poll_buffer + buf_idx, chunk, n);
            buf_idx += n;
            poll_buffer[buf_idx] = '\0';
            data_read = true;
        } else {
            buf_idx = 0;
        }
    }

    if (data_read) {
        if (process_metadata_stream()) {
            resize_pending = true;
        }
    }

    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close(player_fd);
        player_fd = -1;
        clear_metadata();
        resize_pending = true;
    }
}

static void maintain_subprocesses(char **args, long now) {
    static long last_check = 0;

    if (ctrl_pid > 0 && (now - last_check > 500)) {
        if (waitpid(ctrl_pid, NULL, WNOHANG) > 0) ctrl_pid = -1;
        last_check = now;
    }

    if (player_fd == -1 && (now - last_player_fail_ms > 2000)) {
        DBG("Attempting playerctl restart...");
        metadata_pid = spawn_playerctl(args, &player_fd);
        if (player_fd != -1) {
            fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL) | O_NONBLOCK);
        }
        last_player_fail_ms = now;
    }
}

int main() {
    setlocale(LC_ALL, "");
    if (getenv("YAMS_EXIT_IMMEDIATELY")) { init_ui(); cleanup_ui(0); return 0; }
    init_debug();
    signal(SIGINT,  handle_exit);
    signal(SIGTERM, handle_exit);
    signal(SIGWINCH, handle_winch);

    init_ui();
    nodelay(stdscr, TRUE);

    const char *fmt = "PLAYER|{{playerName}}#STATUS|{{status}}#TITLE|{{xesam:title}}#"
        "ARTIST|{{xesam:artist}}#ALBUM|{{xesam:album}}#PATH|{{xesam:url}}#"
        "LENGTH|{{mpris:length}}#GENRE|{{xesam:genre}}#COMPOSER|{{xesam:composer}}#"
        "ART|{{mpris:artUrl}}#LYRICS|{{xesam:asText}}#[[END]]";

    char *meta_args[] = {"playerctl", "metadata", "--follow", "--format", (char*)fmt, NULL};
    metadata_pid = spawn_playerctl(meta_args, &player_fd);
    
    if (player_fd != -1)
        fcntl(player_fd, F_SETFL, fcntl(player_fd, F_GETFL, 0) | O_NONBLOCK);

    while (1) {
        long now = get_ms();
        long elapsed_since_change = now - get_last_metadata_change_time();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        int max_fd = STDIN_FILENO;

        if (player_fd != -1) {
            FD_SET(player_fd, &fds);
            if (player_fd > max_fd) max_fd = player_fd;
        }

        maintain_subprocesses(meta_args, now);

        struct timeval tv;

        if (elapsed_since_change < get_art_debounce()) {
            tv.tv_sec = 0;
            tv.tv_usec = 50000;
        } else if (resize_pending) {
            tv.tv_sec = 0;
            tv.tv_usec = 10000; 
        } else {
            tv.tv_sec = 1;
            tv.tv_usec = 0;
        }

        int activity = select(max_fd + 1, &fds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (activity > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            int ch;
            while ((ch = getch()) != ERR) handle_keypress(ch, now);
            resize_pending = true; 
        }

        if (activity > 0 && player_fd != -1 && FD_ISSET(player_fd, &fds)) {
            handle_player_data();
        }

        bool needs_art_check = (elapsed_since_change >= get_art_debounce() && elapsed_since_change < get_art_debounce() + 100);

        if (resize_pending || needs_art_check) {
            // Check terminal size
            struct winsize size;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
                if (size.ws_row != LINES || size.ws_col != COLS) {
                    resizeterm(size.ws_row, size.ws_col);
                    erase(); 
                    resize_pending = true; // Ensure we re-layout
                }
            }

            // Always draw if pending or if we just hit the art debounce timer
            draw_dashboard();
            last_draw_time = now;

            // Only clear the pending flag if we aren't still waiting for the art timer
            if (elapsed_since_change > get_art_debounce()) {
                resize_pending = false;
            }
        }

        usleep(10000);
    }

    draw_dashboard();

    handle_exit(0);
    return 0;
}
