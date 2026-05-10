#include "panes.h"
#include "render_utils.h"
#include "metadata.h"

void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    werase(win);

    int cur_y = 0;
    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s", safe_str(m.player), safe_str(m.status));

    draw_metadata_line(win, &cur_y, "Player:   ", player_info, 1, true);
    draw_metadata_line(win, &cur_y, "Duration: ", m.length_str, 1, true);
    draw_metadata_line(win, &cur_y, "Title:    ", safe_str(m.title), 1, true);

    draw_metadata_line(win, &cur_y, "Artist:   ", safe_str(m.artist), 2, false);
    draw_metadata_line(win, &cur_y, "Album:    ", safe_str(m.album), 2, false);
    draw_metadata_line(win, &cur_y, "Genre:    ", safe_str(m.genre), 2, false);
    draw_metadata_line(win, &cur_y, "Composer: ", safe_str(m.composer), 2, false);

    char disp_url[1024];
    format_display_url(disp_url, m.file_path, sizeof(disp_url));
    draw_metadata_line(win, &cur_y, "Source:   ", disp_url, 0, false);

    cur_y++; 
    draw_help_text(win, cur_y, 0);
    wnoutrefresh(win);
}

void render_lyrics_pane(WINDOW *win) {
    if (!win) return;
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
