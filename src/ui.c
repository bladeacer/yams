#include <ncurses.h>
#include <panel.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

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

void show_status_msg(const char *msg) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = '\0';
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

    if (player_active && !was_active && curscr) {
        int mh = getmaxy(main_win);
        for (int r = 0; r < mh; r++)
            wredrawln(curscr, 1 + r, 1);
    }

    if (!player_active) {
        int art_top = 2;
        for (int r = 0; r < geo.art_h && art_top + r <= LINES; r++)
            wredrawln(curscr, art_top - 1 + r, 1);
    }

    if (status_msg[0]) {
        int my, mx;
        getmaxyx(stdscr, my, mx);
        mvwprintw(stdscr, my - 1, 1, "%-*s", mx - 2, status_msg);
        wnoutrefresh(stdscr);
        status_msg[0] = '\0';
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
        bool must_resize = (art_update_requested && elapsed < get_art_debounce());

        if (song_changed || must_resize || is_retry_pending()) {
            const char *art_src = (strlen(snap_art_url) > 5) ? snap_art_url : snap_path;
            char title_sig[2048];
            snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(snap_title), safe_str(snap_artist));

            update_and_render_art(art_src, title_sig, geo.art_w, geo.art_h, must_resize);

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
