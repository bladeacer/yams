#include "panes.h"
#include "render_utils.h"
#include "metadata.h"
#include <string.h>

void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    redrawwin(win);
    werase(win);

    MetadataSnap snap;
    snapshot_metadata(&snap);

    int cur_y = 0;
    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s", safe_str(snap.player), safe_str(snap.status));

    draw_metadata_line(win, &cur_y, "Player:   ", player_info, 1, true);
    draw_metadata_line(win, &cur_y, "Duration: ", snap.length_str, 1, true);
    draw_metadata_line(win, &cur_y, "Title:    ", safe_str(snap.title), 1, true);

    draw_metadata_line(win, &cur_y, "Artist:   ", safe_str(snap.artist), 2, false);
    draw_metadata_line(win, &cur_y, "Album:    ", safe_str(snap.album), 2, false);
    draw_metadata_line(win, &cur_y, "Genre:    ", safe_str(snap.genre), 2, false);
    draw_metadata_line(win, &cur_y, "Composer: ", safe_str(snap.composer), 2, false);

    char disp_url[1024];
    format_display_url(disp_url, snap.file_path, sizeof(disp_url));
    draw_metadata_line(win, &cur_y, "Source:   ", disp_url, 0, false);

    cur_y++; 
    draw_help_text(win, cur_y, 0);
    wnoutrefresh(win);
}

void render_lyrics_pane(WINDOW *win) {
    if (!win) return;
    redrawwin(win);
    werase(win);
    
    int h, w;
    getmaxyx(win, h, w);

    if (m.lyric_count > 0) {
        wattron(win, COLOR_PAIR(3));
        for (int i = 0; i < h && i < m.lyric_count; i++) {
            if (m.lyrics[i]) mvwaddnstr(win, i, 0, m.lyrics[i], w);
        }
        wattroff(win, COLOR_PAIR(3));
    } else {
        wattron(win, A_DIM);
        mvwprintw(win, 0, 0, "  (No lyrics found)");
        wattroff(win, A_DIM);
    }
    wnoutrefresh(win); 
}
