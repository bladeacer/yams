#include <panel.h>
#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"
#include "debug.h"

bool resize_pending = false;
int scroll_pos = 0; 
static int last_scroll_pos = -1;
static time_t last_update_time = 0;

static WINDOW *main_win = NULL;
static PANEL  *main_panel = NULL;
static WINDOW *lyr_sub = NULL;
static WINDOW *meta_sub = NULL;

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
    (void)sig;
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
    
    /* * THE FIX: 
     * 1. -fuzz 10% -trim: Removes edges that are ALMOST gray/black.
     * 2. -gravity center -crop 1:1 +repage: Forces a square.
     * 3. -shave 1x1: Removes a 1px border that often carries "ghost" colors.
     */
    snprintf(cmd, sizeof(cmd), 
            "(ffmpeg -hide_banner -loglevel error -i \"%s\" -vframes 1 -f mjpeg - 2>/dev/null) | "
            "magick - "
            "-bordercolor white -border 1x1 " // Wrap in 1px white to ensure a clean start
            "-fuzz 15%% -trim +repage "       // Trim anything within 15% of the border color
            "-shave 1x1 "                     // Remove that 1px safety border
            "-thumbnail %dx%d "               // Scale (remove the '^' to keep ratio)
            "-gravity center -extent %dx%d "  // Pad with transparency/black to keep UI stable
            "webp:- | "
            "jp2a -b --width=%d --height=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'", 
            safe_path, w*2, h*2, w, h, w, h, x + 1);

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
    static long last_art_render_ms = 0;

    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status && strlen(m.status) > 0 && strcmp(m.status, "Stopped") != 0);
    
    // Check for resize or player status change
    bool structural_change = (max_x != last_x || max_y != last_y) || resize_pending;
    bool metadata_change = (strcmp(current_source, rendered_path_cache) != 0);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // IF structural change OR metadata change, we MUST rebuild windows to prevent text loss
    if (structural_change || metadata_change) {
        resize_pending = false;
        last_x = max_x; last_y = max_y;
        
        // Re-init windows
        if (main_panel) del_panel(main_panel);
        if (main_win) delwin(main_win);
        if (lyr_sub) delwin(lyr_sub);
        if (meta_sub) delwin(meta_sub);

        int safe_h = max_y - 2;
        int safe_w = max_x - 2;
        main_win = newwin(safe_h, safe_w, 1, 1);
        main_panel = new_panel(main_win);
        
        erase();
        wnoutrefresh(stdscr);

        if (!player_active) {
            const char* msg = "Awaiting Player Input...";
            wattron(main_win, A_DIM);
            mvwprintw(main_win, safe_h/2, (safe_w - strlen(msg))/2, "%s", msg);
            wattroff(main_win, A_DIM);
            update_panels();
            doupdate();
            return;
        }

        // Layout calcs
        int art_x = 2, art_y = 0, art_w, art_h;
        int lx, ly, lw, lh, mx, my, mw, mh;
        bool horizontal = (max_x > 90);

        if (!horizontal) {
            art_w = safe_w - 6;
            art_h = (int)(art_w * 0.48); 
            if (art_h > (safe_h - 12)) art_h = safe_h - 12;
            my = art_h + 1; mx = 2; mw = safe_w - 4; mh = 5;
            ly = my + mh + 1; lx = 2; lw = safe_w - 4; lh = safe_h - ly;
        } else {
            int split = (int)(safe_w * 0.60);
            art_w = split - 6;
            art_h = (int)(art_w * 0.48); 
            if (art_h > (safe_h - 8)) art_h = safe_h - 8;
            my = art_h + 1; mx = 2; mw = split - 4; mh = 5;
            lx = split + 2; ly = 0; lw = safe_w - lx - 2; lh = safe_h;
        }

        meta_sub = derwin(main_win, mh, mw, my, mx);
        lyr_sub = derwin(main_win, lh, lw, ly, lx);

        // ALWAYS draw text immediately
        werase(meta_sub);
        wattron(meta_sub, A_BOLD | COLOR_PAIR(1));
        mvwprintw(meta_sub, 0, 0, "%s | %s", safe_str(m.status), safe_str(m.player));
        wattroff(meta_sub, A_BOLD);
        mvwprintw(meta_sub, 1, 0, "Title: %.*s", mw-7, safe_str(m.title));
        wattron(meta_sub, COLOR_PAIR(2));
        mvwprintw(meta_sub, 2, 0, "Artist: %.*s", mw-8, safe_str(m.artist));
        mvwprintw(meta_sub, 3, 0, "Album:  %.*s", mw-8, safe_str(m.album));
        wattroff(meta_sub, COLOR_PAIR(2));

        werase(lyr_sub);
        wattron(lyr_sub, COLOR_PAIR(3));
        for (int i = 0; i < lh && i < m.lyric_count; i++) {
            if (m.lyrics[i]) mvwaddnstr(lyr_sub, i, 0, m.lyrics[i], lw);
        }
        wattroff(lyr_sub, COLOR_PAIR(3));

        update_panels();
        doupdate();

        // DEBOUNCED ART RENDER: Only render image if we haven't rendered one in 400ms
        // This stops the "text disappears" bug because we finished drawing text ABOVE.
        if (now_ms - last_art_render_ms > 400 || structural_change) {
            render_art(art_x + 1, art_y + 1, art_w - 1, art_h - 1);
            last_art_render_ms = now_ms;
            strncpy(rendered_path_cache, current_source, 4095);
        }
    }
}
