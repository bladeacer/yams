#include "panes.h"
#include "render_utils.h"
#include "metadata.h"
#include <string.h>

void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    redrawwin(win);
    werase(win);

    char snap_status[64], snap_player[64], snap_length[32];
    char snap_title[512], snap_artist[512], snap_album[512];
    char snap_genre[256], snap_composer[256], snap_path[2048];
    strncpy(snap_status, m.status, sizeof(snap_status) - 1); snap_status[sizeof(snap_status) - 1] = '\0';
    strncpy(snap_player, m.player, sizeof(snap_player) - 1); snap_player[sizeof(snap_player) - 1] = '\0';
    strncpy(snap_length, m.length_str, sizeof(snap_length) - 1); snap_length[sizeof(snap_length) - 1] = '\0';
    strncpy(snap_title, m.title, sizeof(snap_title) - 1); snap_title[sizeof(snap_title) - 1] = '\0';
    strncpy(snap_artist, m.artist, sizeof(snap_artist) - 1); snap_artist[sizeof(snap_artist) - 1] = '\0';
    strncpy(snap_album, m.album, sizeof(snap_album) - 1); snap_album[sizeof(snap_album) - 1] = '\0';
    strncpy(snap_genre, m.genre, sizeof(snap_genre) - 1); snap_genre[sizeof(snap_genre) - 1] = '\0';
    strncpy(snap_composer, m.composer, sizeof(snap_composer) - 1); snap_composer[sizeof(snap_composer) - 1] = '\0';
    strncpy(snap_path, m.file_path, sizeof(snap_path) - 1); snap_path[sizeof(snap_path) - 1] = '\0';

    int cur_y = 0;
    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s", safe_str(snap_player), safe_str(snap_status));

    draw_metadata_line(win, &cur_y, "Player:   ", player_info, 1, true);
    draw_metadata_line(win, &cur_y, "Duration: ", snap_length, 1, true);
    draw_metadata_line(win, &cur_y, "Title:    ", safe_str(snap_title), 1, true);

    draw_metadata_line(win, &cur_y, "Artist:   ", safe_str(snap_artist), 2, false);
    draw_metadata_line(win, &cur_y, "Album:    ", safe_str(snap_album), 2, false);
    draw_metadata_line(win, &cur_y, "Genre:    ", safe_str(snap_genre), 2, false);
    draw_metadata_line(win, &cur_y, "Composer: ", safe_str(snap_composer), 2, false);

    char disp_url[1024];
    format_display_url(disp_url, snap_path, sizeof(disp_url));
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
