#include <ncurses.h>
#include <panel.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#include "ui.h"
#include "layout.h"
#include "panes.h"
#include "art_manager.h"
#include "render_utils.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"
#include "config.h"

bool resize_pending = false;

static char status_msg[256] = {0};
static long status_msg_time = 0;

void show_status_msg(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
    status_msg_time = get_ms();
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char last_known_title[512] = {0};
    static char last_known_artist[512] = {0};
    static bool art_update_requested = false;
    static bool was_active = false;

    char snap_status[64];
    char snap_title[512];
    char snap_artist[512];
    char snap_art_url[2048];
    char snap_path[2048];
    strncpy(snap_status, m.status, sizeof(snap_status) - 1); snap_status[sizeof(snap_status) - 1] = '\0';
    strncpy(snap_title, m.title, sizeof(snap_title) - 1); snap_title[sizeof(snap_title) - 1] = '\0';
    strncpy(snap_artist, m.artist, sizeof(snap_artist) - 1); snap_artist[sizeof(snap_artist) - 1] = '\0';
    strncpy(snap_art_url, m.art_url, sizeof(snap_art_url) - 1); snap_art_url[sizeof(snap_art_url) - 1] = '\0';
    strncpy(snap_path, m.file_path, sizeof(snap_path) - 1); snap_path[sizeof(snap_path) - 1] = '\0';

    if (max_x != geo.max_x || max_y != geo.max_y) {
        recreate_layout();
        clearok(curscr, TRUE);
        resize_pending = true;
        was_active = false;
    }

    bool player_active = (snap_status[0] != '\0' && snap_status[0] != 'S');

    if (player_active && !was_active) {
        clear_art_area();
        werase(main_win);
        if (!art_update_requested)
            reset_art();
    } else if (!player_active && was_active) {
        clear_art_area();
        werase(main_win);
        reset_art();
    }

    bool identity_changed = (strcmp(snap_title, last_known_title) != 0 ||
                         strcmp(snap_artist, last_known_artist) != 0);

    if (identity_changed && (snap_title[0] || snap_artist[0])) {
        strncpy(last_known_title, snap_title, sizeof(last_known_title)-1);
        strncpy(last_known_artist, snap_artist, sizeof(last_known_artist)-1);
        reset_art();
        art_update_requested = true;
    }

    if (art_refresh_requested) {
        art_update_requested = true;
        art_refresh_requested = false;
    }

    if (!player_active) {
        draw_fallback_screen(main_win, geo.max_y - 2, geo.max_x - 2);
    } else {
        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub)  render_lyrics_pane(lyr_sub);
    }

    update_panels();

    if ((player_active && !was_active) || (!player_active && was_active)) {
        if (curscr) {
            int mh = getmaxy(main_win);
            for (int r = 0; r < mh; r++)
                wredrawln(curscr, 1 + r, 1);
        }
    }

    if (status_msg[0]) {
        long now = get_ms();
        if (now - status_msg_time > 3000) {
            status_msg[0] = '\0';
        } else {
            int my, mx;
            getmaxyx(stdscr, my, mx);
            mvwprintw(stdscr, my - 1, 1, "%-*s", mx - 2, status_msg);
            wnoutrefresh(stdscr);
        }
    }

    doupdate();

    if (player_active)
        replay_art_buffer();

    if (!player_active)
        resize_pending = false;

    if (player_active) {
        long now = get_ms();
        long elapsed = now - get_last_metadata_change_time();

        bool song_changed = (art_update_requested && elapsed >= get_art_debounce());

        if (song_changed || is_retry_pending()) {
            const char *art_src = (strlen(snap_art_url) > 5) ? snap_art_url : snap_path;
            char title_sig[2048];
            snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(snap_title), safe_str(snap_artist));

            update_and_render_art(art_src, title_sig, geo.art_w, geo.art_h, false);

            replay_art_buffer();

            art_update_requested = false;
            resize_pending = false;
        }
    }

    was_active = player_active;
}

void show_art_mode_selector(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int vis_rows = max_y - 7;
    if (vis_rows > ART_MODE_COUNT) vis_rows = ART_MODE_COUNT;
    if (vis_rows < 4) vis_rows = 4;

    int w = 44;
    int h = vis_rows + 6;
    int sy = (max_y - h) / 2;
    int sx = (max_x - w) / 2;

    WINDOW *mwin = newwin(h, w, sy, sx);
    PANEL *mpanel = new_panel(mwin);

    wbkgd(mwin, COLOR_PAIR(3));
    keypad(mwin, TRUE);
    nodelay(mwin, FALSE);
    curs_set(1);

    int scroll = 0;
    int current = get_art_mode();

    char filter[64] = {0};
    int filter_len = 0;

    int matched[ART_MODE_COUNT];
    int match_count = ART_MODE_COUNT;
    for (int i = 0; i < ART_MODE_COUNT; i++) matched[i] = i;

    while (true) {
        if (filter_len == 0) {
            match_count = ART_MODE_COUNT;
            for (int i = 0; i < ART_MODE_COUNT; i++) matched[i] = i;
        } else {
            match_count = 0;
            for (int i = 0; i < ART_MODE_COUNT; i++) {
                const char *p = art_modes[i].name;
                const char *f = filter;
                while (*p && *f) {
                    if (tolower((unsigned char)*p) == tolower((unsigned char)*f)) {
                        f++;
                    }
                    p++;
                }
                if (*f == '\0') {
                    matched[match_count++] = i;
                }
            }
            if (current >= ART_MODE_COUNT) current = 0;
        }

        if (match_count == 1) {
            current = matched[0];
        }

        int sel_idx = -1;
        for (int i = 0; i < match_count; i++) {
            if (matched[i] == current) { sel_idx = i; break; }
        }
        if (sel_idx < 0 && match_count > 0) {
            current = matched[0];
            sel_idx = 0;
        }

        if (sel_idx < scroll) scroll = sel_idx;
        if (sel_idx >= scroll + vis_rows) scroll = sel_idx - vis_rows + 1;

        werase(mwin);
        box(mwin, 0, 0);
        mvwprintw(mwin, 0, 2, " Art Mode ");

        int line = 1;
        for (int i = scroll; i < match_count && line <= vis_rows; i++, line++) {
            int mid = matched[i];
            if (mid == current) {
                wattron(mwin, A_REVERSE);
                mvwprintw(mwin, line, 2, "%s", art_modes[mid].name);
                int pad = w - 4 - (int)strlen(art_modes[mid].name);
                if (pad > 0) mvwprintw(mwin, line, 2 + (int)strlen(art_modes[mid].name), "%*s", pad, "");
                wattroff(mwin, A_REVERSE);
            } else {
                mvwprintw(mwin, line, 2, "%s", art_modes[mid].name);
            }
        }

        mvwprintw(mwin, h - 5, 2, "arrows/vim  scroll  ENTER select");
        mvwprintw(mwin, h - 4, 2, "ESC quit  Type to filter");

        mvwprintw(mwin, h - 3, 2, "Filter: ");
        if (filter_len > 0) {
            int max_fw = w - 12;
            int vis_len = filter_len;
            int start = 0;
            if (vis_len > max_fw) {
                start = vis_len - max_fw;
                vis_len = max_fw;
            }
            wattron(mwin, A_BOLD);
            mvwprintw(mwin, h - 3, 10, "%.*s", vis_len, filter + start);
            wattroff(mwin, A_BOLD);
        }

        if (match_count == 0) {
            mvwprintw(mwin, 1, 2, "(no matches)");
        }

        update_panels();
        doupdate();

        int ch = wgetch(mwin);
        if (ch == 27 || ch == 'q') {
            if (filter_len > 0) {
                filter[0] = '\0';
                filter_len = 0;
                continue;
            }
            break;
        } else if (ch == '\n' || ch == KEY_ENTER || ch == '\r') {
            if (match_count > 0) {
                set_art_mode(current);
            }
            break;
        } else if (ch == KEY_UP || ch == 'k' || ch == 16) {
            if (match_count > 0) {
                for (int i = 0; i < match_count; i++) {
                    if (matched[i] == current) {
                        if (i > 0) { current = matched[i - 1]; }
                        break;
                    }
                }
            }
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 14) {
            if (match_count > 0) {
                for (int i = 0; i < match_count; i++) {
                    if (matched[i] == current) {
                        if (i < match_count - 1) { current = matched[i + 1]; }
                        break;
                    }
                }
            }
        } else if (ch == KEY_PPAGE) {
            if (match_count > 0) {
                for (int i = 0; i < match_count; i++) {
                    if (matched[i] == current) {
                        int ni = i - vis_rows;
                        if (ni < 0) ni = 0;
                        current = matched[ni];
                        break;
                    }
                }
            }
        } else if (ch == KEY_NPAGE) {
            if (match_count > 0) {
                for (int i = 0; i < match_count; i++) {
                    if (matched[i] == current) {
                        int ni = i + vis_rows;
                        if (ni >= match_count) ni = match_count - 1;
                        current = matched[ni];
                        break;
                    }
                }
            }
        } else if (ch == 8 || ch == 127 || ch == KEY_BACKSPACE) {
            if (filter_len > 0) {
                filter[--filter_len] = '\0';
            }
        } else if (ch >= 32 && ch <= 126) {
            if (filter_len < (int)sizeof(filter) - 1) {
                filter[filter_len++] = (char)ch;
                filter[filter_len] = '\0';
            }
        } else if (ch == 21) {
            filter[0] = '\0';
            filter_len = 0;
        }
    }

    del_panel(mpanel);
    delwin(mwin);
    curs_set(0);

    reset_art();
    resize_pending = true;
}

static char **fetch_player_list(int *count) {
    *count = 0;
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1) return NULL;

    pid_t pid = fork();
    if (pid == -1) { close(pipe_fds[0]); close(pipe_fds[1]); return NULL; }
    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
        char *args[] = {"playerctl", "-l", NULL};
        execvp("playerctl", args);
        _exit(1);
    }
    close(pipe_fds[1]);

    char buf[4096];
    ssize_t total = 0;
    ssize_t n;
    while ((n = read(pipe_fds[0], buf + total, sizeof(buf) - 1 - (size_t)total)) > 0)
        total += n;
    close(pipe_fds[0]);
    int w;
    waitpid(pid, &w, 0);
    if (total <= 0) return NULL;
    buf[total] = '\0';

    char **list = NULL;
    int cap = 0;
    char *line = strtok(buf, "\n");
    while (line) {
        if (line[0]) {
            if (*count >= cap) {
                cap = cap ? cap * 2 : 8;
                list = realloc(list, sizeof(char*) * (size_t)cap);
            }
            list[*count] = strdup(line);
            (*count)++;
        }
        line = strtok(NULL, "\n");
    }
    return list;
}

void show_player_selector(void) {
    int count = 0;
    char **players = fetch_player_list(&count);
    if (!players) {
        show_status_msg("No players available");
        return;
    }

    const char *current = get_player_name();
    int current_idx = (current[0] == '\0') ? 0 : -1;
    if (current_idx == -1) {
        for (int i = 0; i < count; i++) {
            if (strcmp(players[i], current) == 0) { current_idx = i + 1; break; }
        }
        if (current_idx == -1) current_idx = 0;
    }

    int total_items = count + 1;
    char **labels = malloc(sizeof(char*) * (size_t)total_items);
    labels[0] = strdup("Auto (last active)");
    for (int i = 0; i < count; i++)
        labels[i + 1] = strdup(players[i]);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int vis_rows = total_items < 10 ? total_items : 10;
    int w = 40;
    int h = vis_rows + 6;
    if (h > max_y - 2) h = max_y - 2;
    int sy = (max_y - h) / 2;
    int sx = (max_x - w) / 2;

    WINDOW *pwin = newwin(h, w, sy, sx);
    PANEL *ppanel = new_panel(pwin);
    wbkgd(pwin, COLOR_PAIR(3));
    keypad(pwin, TRUE);
    nodelay(pwin, FALSE);
    curs_set(1);

    int scroll = 0;
    int sel = current_idx;
    if (sel < 0) sel = 0;

    while (1) {
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + vis_rows) scroll = sel - vis_rows + 1;

        werase(pwin);
        box(pwin, 0, 0);
        mvwprintw(pwin, 0, 2, " Select Player ");

        for (int i = scroll; i < total_items && i - scroll < vis_rows; i++) {
            int line = 1 + i - scroll;
            if (i == sel) {
                wattron(pwin, A_REVERSE);
                mvwprintw(pwin, line, 2, "%s", labels[i]);
                int pad = w - 4 - (int)strlen(labels[i]);
                if (pad > 0) mvwprintw(pwin, line, 2 + (int)strlen(labels[i]), "%*s", pad, "");
                wattroff(pwin, A_REVERSE);
            } else {
                mvwprintw(pwin, line, 2, "%s", labels[i]);
            }
        }

        mvwprintw(pwin, h - 4, 2, "arrows/vim  scroll  ENTER select");
        mvwprintw(pwin, h - 3, 2, "ESC  quit");

        update_panels();
        doupdate();

        int ch = wgetch(pwin);
        if (ch == 27 || ch == 'q') break;
        if (ch == '\n' || ch == KEY_ENTER || ch == '\r') {
            if (sel == 0) {
                set_player_name("");
            } else {
                set_player_name(players[sel - 1]);
            }
            break;
        }
        if ((ch == KEY_UP || ch == 'k') && sel > 0) sel--;
        if ((ch == KEY_DOWN || ch == 'j') && sel < total_items - 1) sel++;
        if (ch == KEY_PPAGE) sel = sel - vis_rows > 0 ? sel - vis_rows : 0;
        if (ch == KEY_NPAGE) sel = sel + vis_rows < total_items - 1 ? sel + vis_rows : total_items - 1;
    }

    del_panel(ppanel);
    delwin(pwin);
    curs_set(0);

    for (int i = 0; i < count; i++) free(players[i]);
    free(players);
    for (int i = 0; i < total_items; i++) free(labels[i]);
    free(labels);

    reset_art();
    resize_pending = true;
}
