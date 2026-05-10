#include <ncurses.h>
#include <panel.h>
#include <string.h>

#include "ui.h"
#include "layout.h"
#include "panes.h"
#include "art_manager.h"
#include "render_utils.h"
#include "metadata.h"
#include "utils.h"

bool resize_pending = false;

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // 1. Handle Resizing
    if (max_x != geo.max_x || max_y != geo.max_y) {
        recreate_layout();
        clearok(curscr, TRUE);
    }

    // 2. State Check
    bool player_active = (m.status[0] != '\0' && m.status[0] != 'S');

    if (!player_active) {
        draw_fallback_screen(main_win, geo.max_y - 2, geo.max_x - 2);
    } else {
        // 3. Render Components (to internal buffers)
        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub)  render_lyrics_pane(lyr_sub);
    }

    // 4. Push updates to the physical terminal
    // EVERYTHING ncurses-related must happen before art rendering
    update_panels();
    doupdate();

    // 5. Manage Album Art (AFTER doupdate)
    if (player_active) {
        const char *art_src = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
        char title_sig[2048];
        snprintf(title_sig, sizeof(title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));

        // This prints directly to the terminal, so it must be the LAST thing we do
        update_and_render_art(art_src, title_sig, geo.art_w, geo.art_h, resize_pending);
    }
}
