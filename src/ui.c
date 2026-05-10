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

bool resize_pending = false;

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char last_known_title[512] = {0};
    static char last_known_artist[512] = {0};
    static bool art_update_requested = false;

    // 1. Handle Resizing
    if (max_x != geo.max_x || max_y != geo.max_y) {
        recreate_layout();
        clearok(curscr, TRUE);
        resize_pending = true; 
    }

    bool player_active = (m.status[0] != '\0' && m.status[0] != 'S');
    
    // 2. Identity Check
    bool song_changed = (strcmp(m.title, last_known_title) != 0 || 
                         strcmp(m.artist, last_known_artist) != 0);

    if (song_changed) {
        strncpy(last_known_title, m.title, sizeof(last_known_title)-1);
        strncpy(last_known_artist, m.artist, sizeof(last_known_artist)-1);
        reset_art(); // Reset internal manager timers
        art_update_requested = true; 
    }

    if (resize_pending) {
        art_update_requested = true;
    }

    // 3. Ncurses Updates (Text stuff)
    if (!player_active) {
        draw_fallback_screen(main_win, geo.max_y - 2, geo.max_x - 2);
    } else {
        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub)  render_lyrics_pane(lyr_sub);
    }

    update_panels();
    doupdate();

    if (player_active && art_update_requested) {
        const char *art_src = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
        char title_sig[2048];
        snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));

        // We check how long since the change globally
        long elapsed = get_ms() - get_last_metadata_change_time();

        // We force the redraw if we are in the "active" zone for a new song
        // or if a terminal resize actually happened.
        bool force_art = (elapsed < 500) || resize_pending;

        update_and_render_art(art_src, title_sig, geo.art_w, geo.art_h, force_art);

        if (get_ms() - get_last_metadata_change_time() > 300) {
            art_update_requested = false;
        }
    }

    resize_pending = false; 
}
