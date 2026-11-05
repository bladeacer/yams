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
#include <sys/ioctl.h>
#include <fcntl.h>

#include "ui.h"
#include "selector_layout.h"
#include "art.h"
#include "layout.h"
#include "panes.h"
#include "art_manager.h"
#include "render_utils.h"
#include "metadata.h"
#include "subprocess.h"
#include "utils.h"
#include "debug.h"
#include "config.h"

volatile sig_atomic_t resize_pending = false;

static char status_msg[256] = {0};
static long status_msg_time = 0;

void show_status_msg(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
    status_msg_time = get_ms();
}

typedef struct {
    char last_title[512];
    char last_artist[512];
    char last_path[2048];
    bool art_update_requested;
    bool was_active;
} DashboardState;

static DashboardState dashboard;

static void draw_status_line(void) {
    long now = get_ms();
    int my, mx;
    getmaxyx(stdscr, my, mx);

    if (status_msg[0] && now - status_msg_time > 3000) {
        status_msg[0] = '\0';
        mvwprintw(stdscr, my - 1, 1, "%*s", mx - 2, "");
        wnoutrefresh(stdscr);
    } else if (status_msg[0]) {
        mvwprintw(stdscr, my - 1, 1, "%-*s", mx - 2, status_msg);
        wnoutrefresh(stdscr);
    }
}

static void check_layout_change(int max_x, int max_y) {
    if (max_x != geo.max_x || max_y != geo.max_y) {
        recreate_layout();
        clearok(curscr, TRUE);
        resize_pending = true;
        dashboard.was_active = false;
    }
}

static void update_player_activity(bool player_active) {
    if (player_active && !dashboard.was_active) {
        clear_art_area();
        werase(main_win);
        if (!dashboard.art_update_requested)
            reset_art();
    } else if (!player_active && dashboard.was_active) {
        clear_art_area();
        werase(main_win);
        reset_art();
    }
}

static void update_identity(const MetadataSnap *snap) {
    bool identity_changed = (strcmp(snap->title, dashboard.last_title) != 0 ||
                             strcmp(snap->artist, dashboard.last_artist) != 0 ||
                             strcmp(snap->file_path, dashboard.last_path) != 0);

    if (identity_changed &&
        (snap->title[0] || snap->artist[0] || snap->file_path[0])) {
        strncpy(dashboard.last_title, snap->title, sizeof(dashboard.last_title) - 1);
        strncpy(dashboard.last_artist, snap->artist, sizeof(dashboard.last_artist) - 1);
        strncpy(dashboard.last_path, snap->file_path, sizeof(dashboard.last_path) - 1);
        dashboard.last_title[sizeof(dashboard.last_title) - 1] = '\0';
        dashboard.last_artist[sizeof(dashboard.last_artist) - 1] = '\0';
        dashboard.last_path[sizeof(dashboard.last_path) - 1] = '\0';
        reset_art();
        dashboard.art_update_requested = true;
    }

    if (art_refresh_requested) {
        dashboard.art_update_requested = true;
        art_refresh_requested = false;
    }
}

static void render_new_art(const MetadataSnap *snap) {
    long now = get_ms();
    long elapsed = now - get_last_metadata_change_time();
    bool song_changed = (dashboard.art_update_requested && elapsed >= get_art_debounce());

    if (!song_changed && !is_retry_pending())
        return;

    const char *art_src = art_source_for_current_track();
    char title_sig[2048];
    snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(snap->title),
             safe_str(snap->artist));

    update_and_render_art(art_src ? art_src : "", title_sig, geo.art_w,
                          geo.art_h, false);

    replay_art_buffer();

    dashboard.art_update_requested = false;
    resize_pending = false;
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    MetadataSnap snap;
    snapshot_metadata(&snap);

    check_layout_change(max_x, max_y);

    bool player_active = (snap.status[0] != '\0' && snap.status[0] != 'S');
    bool was_active = dashboard.was_active;

    update_player_activity(player_active);
    update_identity(&snap);

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

    draw_status_line();
    doupdate();

    if (player_active)
        replay_art_buffer();

    if (!player_active)
        resize_pending = false;

    if (player_active)
        render_new_art(&snap);

    dashboard.was_active = player_active;
}


static bool fuzzy_match(const char *pattern, const char *text) {
    while (*text && *pattern) {
        if (tolower((unsigned char)*text) == tolower((unsigned char)*pattern))
            pattern++;
        text++;
    }
    return *pattern == '\0';
}

static int matched_position(const int *matched, int count, int current) {
    for (int i = 0; i < count; i++) {
        if (matched[i] == current) return i;
    }
    return -1;
}

/* Shared modal list selector. Returns the selected index, or -1 on cancel. */
static int run_list_selector(const char *title, const char *const *items,
                             int count, int initial, bool filterable) {
    if (!items || count <= 0) return -1;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int max_item_w = (int)strlen(title);
    for (int i = 0; i < count; i++) {
        int item_w = (int)strlen(items[i]);
        if (item_w > max_item_w) max_item_w = item_w;
    }

    SelectorLayout lo;
    if (!compute_selector_layout(max_y, max_x, count, max_item_w, filterable,
                                 &lo)) {
        show_status_msg("Terminal too small");
        return -1;
    }

    WINDOW *win = newwin(lo.h, lo.w, lo.y, lo.x);
    if (!win) {
        show_status_msg("Could not open selector");
        return -1;
    }
    PANEL *panel = new_panel(win);
    if (!panel) {
        delwin(win);
        show_status_msg("Could not open selector");
        return -1;
    }

    int *matched = malloc(sizeof(int) * (size_t)count);
    if (!matched) {
        del_panel(panel);
        delwin(win);
        show_status_msg("Out of memory");
        return -1;
    }
    for (int i = 0; i < count; i++) matched[i] = i;

    wbkgd(win, COLOR_PAIR(3));
    keypad(win, TRUE);
    wtimeout(win, 100);
    curs_set(1);

    int match_count = count;
    int current = (initial >= 0 && initial < count) ? initial : 0;
    int scroll = 0;
    char filter[64] = {0};
    int filter_len = 0;
    int result = -1;

    while (true) {
        if (exit_requested) break;

        if (filterable) {
            if (filter_len == 0) {
                match_count = count;
                for (int i = 0; i < count; i++) matched[i] = i;
            } else {
                match_count = 0;
                for (int i = 0; i < count; i++) {
                    if (fuzzy_match(filter, items[i]))
                        matched[match_count++] = i;
                }
            }
            if (match_count == 1) current = matched[0];
        }

        int sel_idx = matched_position(matched, match_count, current);
        if (sel_idx < 0 && match_count > 0) {
            current = matched[0];
            sel_idx = 0;
        }

        if (sel_idx >= 0) {
            if (sel_idx < scroll) scroll = sel_idx;
            if (sel_idx >= scroll + lo.vis_rows)
                scroll = sel_idx - lo.vis_rows + 1;
        } else {
            scroll = 0;
        }

        werase(win);
        box(win, 0, 0);
        if (lo.w > 4)
            mvwaddnstr(win, 0, 2, title, lo.w - 4);

        int list_end = (lo.footer_y > 0) ? lo.footer_y : lo.h - 1;
        for (int i = scroll; i < match_count; i++) {
            int row = lo.list_y + (i - scroll);
            if (row >= list_end) break;

            int idx = matched[i];
            int text_w = lo.w - 4;
            if (text_w < 1) text_w = 1;
            if (idx == current) {
                wattron(win, A_REVERSE);
                mvwhline(win, row, 1, ' ', lo.w - 2);
                mvwaddnstr(win, row, 2, items[idx], text_w);
                wattroff(win, A_REVERSE);
            } else {
                mvwhline(win, row, 1, ' ', lo.w - 2);
                mvwaddnstr(win, row, 2, items[idx], text_w);
            }
        }

        if (match_count == 0 && lo.list_y < list_end) {
            mvwaddnstr(win, lo.list_y, 2, "(no matches)", lo.w - 4);
        }

        if (lo.footer_y > 0) {
            int hint_w = lo.w - 4;
            if (hint_w < 1) hint_w = 1;
            mvwaddnstr(win, lo.footer_y, 2,
                       "arrows/vim scroll ENTER select", hint_w);
            if (lo.footer_y + 1 < lo.h - 1)
                mvwaddnstr(win, lo.footer_y + 1, 2,
                           filterable ? "ESC quit  Type to filter" : "ESC  quit",
                           hint_w);

            if (filterable && lo.filter_y > 0) {
                mvwaddnstr(win, lo.filter_y, 2, "Filter: ", hint_w);
                if (filter_len > 0 && lo.filter_w > 0) {
                    int start = 0;
                    int vis_len = filter_len;
                    if (vis_len > lo.filter_w) {
                        start = vis_len - lo.filter_w;
                        vis_len = lo.filter_w;
                    }
                    wattron(win, A_BOLD);
                    mvwaddnstr(win, lo.filter_y, lo.filter_x, filter + start,
                               vis_len);
                    wattroff(win, A_BOLD);
                    wmove(win, lo.filter_y, lo.filter_x + vis_len);
                } else if (lo.filter_w > 0) {
                    wmove(win, lo.filter_y, lo.filter_x);
                }
            }
        }

        update_panels();
        doupdate();

        int ch = wgetch(win);
        if (ch == ERR) continue;
        if (ch == KEY_RESIZE || ch == 410) {
            /* Recompute in place instead of closing the menu. This keeps the
             * selector usable while the terminal is being resized. */
            struct winsize size;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
                if (size.ws_row != LINES || size.ws_col != COLS)
                    resizeterm(size.ws_row, size.ws_col);
                max_y = size.ws_row;
                max_x = size.ws_col;
            } else {
                getmaxyx(stdscr, max_y, max_x);
            }

            SelectorLayout next;
            if (!compute_selector_layout(max_y, max_x, count, max_item_w,
                                         filterable, &next)) {
                show_status_msg("Terminal too small");
                break;
            }
            lo = next;
            wresize(win, lo.h, lo.w);
            mvwin(win, lo.y, lo.x);
            werase(win);
            resize_pending = false;
            continue;
        }
        if (ch == 27) {
            if (filterable && filter_len > 0) {
                filter[0] = '\0';
                filter_len = 0;
                continue;
            }
            break;
        }
        if (ch == 'q' && (!filterable || filter_len == 0))
            break;
        if (ch == '\n' || ch == KEY_ENTER || ch == '\r') {
            if (match_count > 0) result = current;
            break;
        }

        int pos = matched_position(matched, match_count, current);
        if (ch == KEY_UP || ch == 'k' || ch == 16) {
            if (pos > 0) current = matched[pos - 1];
        } else if (ch == KEY_DOWN || ch == 'j' || ch == 14) {
            if (pos >= 0 && pos < match_count - 1) current = matched[pos + 1];
        } else if (ch == KEY_PPAGE) {
            if (pos >= 0) {
                int next = pos - lo.vis_rows;
                if (next < 0) next = 0;
                current = matched[next];
            }
        } else if (ch == KEY_NPAGE) {
            if (pos >= 0) {
                int next = pos + lo.vis_rows;
                if (next >= match_count) next = match_count - 1;
                current = matched[next];
            }
        } else if (filterable && (ch == 8 || ch == 127 || ch == KEY_BACKSPACE)) {
            if (filter_len > 0)
                filter[--filter_len] = '\0';
        } else if (filterable && ch >= 32 && ch <= 126) {
            if (filter_len < (int)sizeof(filter) - 1) {
                filter[filter_len++] = (char)ch;
                filter[filter_len] = '\0';
            }
        } else if (filterable && ch == 21) {
            filter[0] = '\0';
            filter_len = 0;
        }
    }

    free(matched);
    del_panel(panel);
    delwin(win);
    curs_set(0);
    resize_pending = true;
    return result;
}

static void free_player_list(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

static char **fetch_player_list(int *count) {
    *count = 0;
    char *const args[] = {"playerctl", "-l", NULL};
    CommandOutput out;
    if (!capture_command(args, &out))
        return NULL;
    if (out.len == 0) {
        command_output_free(&out);
        return NULL;
    }

    char **list = NULL;
    int cap = 0;
    char *saveptr = NULL;
    char *line = strtok_r(out.data, "\n", &saveptr);
    while (line) {
        if (line[0]) {
            if (*count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                char **tmp = realloc(list, sizeof(char*) * (size_t)new_cap);
                if (!tmp) {
                    free_player_list(list, *count);
                    *count = 0;
                    command_output_free(&out);
                    return NULL;
                }
                list = tmp;
                cap = new_cap;
            }
            char *dup = strdup(line);
            if (!dup) {
                free_player_list(list, *count);
                *count = 0;
                command_output_free(&out);
                return NULL;
            }
            list[*count] = dup;
            (*count)++;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    command_output_free(&out);
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
    int current_idx = 0;
    if (current[0] != '\0') {
        for (int i = 0; i < count; i++) {
            if (strcmp(players[i], current) == 0) {
                current_idx = i + 1;
                break;
            }
        }
    }

    int total_items = count + 1;
    char **labels = calloc((size_t)total_items, sizeof(char*));
    if (!labels) {
        free_player_list(players, count);
        show_status_msg("Out of memory");
        return;
    }
    labels[0] = strdup("Auto (last active)");
    for (int i = 0; i < count; i++)
        labels[i + 1] = strdup(players[i]);
    for (int i = 0; i < total_items; i++) {
        if (!labels[i]) {
            free_player_list(labels, total_items);
            free_player_list(players, count);
            show_status_msg("Out of memory");
            return;
        }
    }

    int sel = run_list_selector("Select Player", (const char *const *)labels,
                                total_items, current_idx, false);
    if (sel == 0) {
        set_player_name("");
    } else if (sel > 0) {
        set_player_name(players[sel - 1]);
    }

    free_player_list(players, count);
    free_player_list(labels, total_items);

    reset_art();
    resize_pending = true;
}

void show_art_mode_selector(void) {
    const char *names[ART_MODE_COUNT];
    for (int i = 0; i < ART_MODE_COUNT; i++)
        names[i] = art_modes[i].name;

    int sel = run_list_selector("Art Mode", names, ART_MODE_COUNT,
                                get_art_mode(), true);
    if (sel >= 0) {
        const ArtModeEntry *mode = &art_modes[sel];
        if (!art_mode_ready(mode->pixel_mode)) {
            char msg[256];
            int fallback = art_fallback_mode(sel);
            snprintf(msg, sizeof(msg), " '%s' unavailable (%s); using '%s'",
                     mode->name,
                     art_mode_unsupported_hint(mode->pixel_mode),
                     art_modes[fallback].name);
            show_status_msg(msg);
            sel = fallback;
        }
        set_art_mode(sel);
    }

    reset_art();
    resize_pending = true;
}
