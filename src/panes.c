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
    if (snap.length_us > 0) {
        char position[96];
        snprintf(position, sizeof(position), "%s / %s",
                 snap.position_str, snap.length_str);
        draw_metadata_line(win, &cur_y, "Position: ", position, 2, false);
    }
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

    if (m.lyric_count <= 0) {
        wattron(win, A_DIM);
        mvwprintw(win, 0, 0, "  (No lyrics found)");
        wattroff(win, A_DIM);
        wnoutrefresh(win);
        return;
    }

    /* Synced lyrics scroll to keep the active line near the middle. Plain text
     * lyrics have no active line and stay pinned at the top. */
    int current = metadata_lyric_index();
    int start = 0;
    if (current >= 0 && m.lyric_count > h) {
        start = current - h / 2;
        if (start < 0) start = 0;
        if (start > m.lyric_count - h) start = m.lyric_count - h;
    }

    for (int row = 0; row < h; row++) {
        int idx = start + row;
        if (idx >= m.lyric_count) break;
        if (!m.lyrics[idx]) continue;

        if (idx == current) {
            /* Active synced line: bold accent so it is obvious at a glance. */
            wattron(win, COLOR_PAIR(4) | A_BOLD);
            mvwaddnstr(win, row, 0, m.lyrics[idx], w);
            wattroff(win, COLOR_PAIR(4) | A_BOLD);
        } else if (current >= 0 && idx < current) {
            wattron(win, COLOR_PAIR(3) | A_DIM);
            mvwaddnstr(win, row, 0, m.lyrics[idx], w);
            wattroff(win, COLOR_PAIR(3) | A_DIM);
        } else {
            wattron(win, COLOR_PAIR(3));
            mvwaddnstr(win, row, 0, m.lyrics[idx], w);
            wattroff(win, COLOR_PAIR(3));
        }
    }

    wnoutrefresh(win);
}
