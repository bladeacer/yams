#include <panel.h>
#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

bool resize_pending = false;
int scroll_pos = 0; // Keeping for compatibility, but not used for shifting
static int last_scroll_pos = -1;

static WINDOW *main_win = NULL;
static PANEL  *main_panel = NULL;
static WINDOW *lyr_sub = NULL;

void init_ui() {
    initscr();
    scrollok(stdscr, FALSE);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, 250, -1);
    }
    curs_set(0); 
    noecho(); 
    keypad(stdscr, TRUE); 
    timeout(20); 
}

static const char* safe_str(const char* s) {
    return (s && strlen(s) > 0) ? s : "Unknown";
}

void cleanup_ui(int sig) {
    endwin();
    printf("\033[2J\033[H\033[?25h\033[0m");
    fflush(stdout);
    exit(0);
}

static void render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return;

    char decoded_path[4096];
    char *raw_path = (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url;
    url_decode(decoded_path, raw_path);
    
    char *safe_path = sh_escape(decoded_path);
    char cmd[16384];
    
    snprintf(cmd, sizeof(cmd), 
        "(ffmpeg -hide_banner -loglevel error -i \"%s\" -vframes 1 -f mjpeg - 2>/dev/null) | "
        "magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --height=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'", 
        safe_path, w*2, h*2, w*2, h*2, w - 1, h, x + 1);

    def_prog_mode(); 
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
        pclose(fp);
    }
    printf("\x1b[0m");
    fflush(stdout);
    reset_prog_mode();
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char rendered_path_cache[4096] = {0};
    static int last_x = 0, last_y = 0;
    
    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool needs_full_rebuild = (strcmp(current_source, rendered_path_cache) != 0) || 
                               (max_x != last_x || max_y != last_y) || resize_pending;

    // 1. FIXED GEOMETRY (No Edges Touched)
    const int meta_h = 6;
    int art_x, art_y, art_w, art_h;
    int lx, ly, lw, lh;
    bool horizontal = (max_x > 90);

    int safe_h = max_y - 2;
    int safe_w = max_x - 2;

    if (!horizontal) {
        art_w = safe_w - 4; art_x = 2; art_y = meta_h + 1;
        art_h = (int)(art_w / (get_image_aspect(current_source) * 2.1));
        if (art_h > (safe_h * 0.4)) art_h = safe_h * 0.4;
        lx = 2; ly = art_y + art_h + 1; lw = safe_w - 4; lh = safe_h - ly - 1;
    } else {
        int split = (int)(safe_w * 0.40);
        art_x = 2; art_y = meta_h + 1; art_w = split - 4;
        art_h = (int)(art_w / (get_image_aspect(current_source) * 2.1));
        lx = split + 2; ly = 1; lw = safe_w - lx - 1; lh = safe_h - 2;
    }

    if (needs_full_rebuild) {
        resize_pending = false;
        last_x = max_x; last_y = max_y;
        strncpy(rendered_path_cache, current_source, 4095);

        if (main_panel) del_panel(main_panel);
        if (main_win) delwin(main_win);
        if (lyr_sub) delwin(lyr_sub);

        main_win = newwin(safe_h, safe_w, 1, 1);
        main_panel = new_panel(main_win);
        lyr_sub = derwin(main_win, lh, lw, ly, lx);
        scrollok(lyr_sub, FALSE);

        erase();
        wnoutrefresh(stdscr);

        wattron(main_win, A_BOLD | COLOR_PAIR(1));
        mvwprintw(main_win, 0, 1, "[%s] %s", safe_str(m.status), safe_str(m.player));
        wattroff(main_win, A_BOLD);

        mvwprintw(main_win, 1, 1, "Title: %.*s", art_w, safe_str(m.title));

        wattron(main_win, COLOR_PAIR(2));
        mvwprintw(main_win, 2, 1, "Artist: %.*s", art_w, safe_str(m.artist));
        mvwprintw(main_win, 3, 1, "Album: %.*s", art_w, safe_str(m.album));
        wattroff(main_win, COLOR_PAIR(2));

        // Static Lyric Print (No scroll offset)
        werase(lyr_sub);
        wattron(lyr_sub, COLOR_PAIR(3));
        for (int i = 0; i < lh && i < m.lyric_count; i++) {
            if (m.lyrics[i]) mvwaddnstr(lyr_sub, i, 0, m.lyrics[i], lw);
        }
        wattroff(lyr_sub, COLOR_PAIR(3));

        update_panels();
        doupdate();
        render_art(art_x + 1, art_y + 1, art_w, art_h);
    }
}
