#include <ncurses.h>
#include <panel.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

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

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char last_known_title[512] = {0};
    static char last_known_artist[512] = {0};
    static bool art_update_requested = false;
    static bool was_active = false;

    // Snapshot metadata at entry for a consistent draw cycle
    char snap_status[64];
    char snap_title[512];
    char snap_artist[512];
    char snap_art_url[2048];
    char snap_path[2048];
    strncpy(snap_status, m.status, sizeof(snap_status) - 1);
    strncpy(snap_title, m.title, sizeof(snap_title) - 1);
    strncpy(snap_artist, m.artist, sizeof(snap_artist) - 1);
    strncpy(snap_art_url, m.art_url, sizeof(snap_art_url) - 1);
    strncpy(snap_path, m.file_path, sizeof(snap_path) - 1);

    if (max_x != geo.max_x || max_y != geo.max_y) {
        recreate_layout();
        clearok(curscr, TRUE);
        resize_pending = true;
        was_active = false;
    }

    bool player_active = (snap_status[0] != '\0' && snap_status[0] != 'S');

    if (player_active && !was_active) {
        clear_art_area();
    }

    bool identity_changed = (strcmp(snap_title, last_known_title) != 0 || 
                         strcmp(snap_artist, last_known_artist) != 0);

    if (identity_changed && (snap_title[0] || snap_artist[0])) {
        strncpy(last_known_title, snap_title, sizeof(last_known_title)-1);
        strncpy(last_known_artist, snap_artist, sizeof(last_known_artist)-1);
        reset_art();
        art_update_requested = true;
    }

    if (resize_pending) {
        art_update_requested = true;
    }

    // Exactly one of fallback or regular panes is rendered per cycle
    if (!player_active) {
        draw_fallback_screen(main_win, geo.max_y - 2, geo.max_x - 2);
        resize_pending = false;
    } else {
        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub)  render_lyrics_pane(lyr_sub);
    }

    update_panels();
    doupdate();

    if (player_active) {
        long elapsed = get_ms() - get_last_metadata_change_time();

        bool song_changed = (art_update_requested && elapsed >= get_art_debounce());

        bool must_resize = resize_pending;

        if (song_changed || must_resize) {
            const char *art_src = (strlen(snap_art_url) > 5) ? snap_art_url : snap_path;
            char title_sig[2048];
            snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(snap_title), safe_str(snap_artist));

            update_and_render_art(art_src, title_sig, geo.art_w, geo.art_h, must_resize);

            art_update_requested = false;
            resize_pending = false;
        }
    }

    was_active = player_active;
}
