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
int scroll_pos = 0;
static WINDOW *lyr_win = NULL;
static int last_scroll_pos = -1;
char last_art_path[4096] = {0};

void init_ui() {
    DBG("UI_INIT: Initializing ncurses");
    initscr();
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, 250, -1);
        DBG("UI_INIT: Colors initialized");
    }
    curs_set(0); 
    noecho(); 
    keypad(stdscr, TRUE); 
    timeout(20); 
}

static const char* safe_str(const char* s) {
    if (!s || strlen(s) == 0) {
        return "Unknown";
    }
    return s;
}

void cleanup_ui(int sig) {
    DBG("UI_CLEANUP: Signal %d received, closing ncurses", sig);
    clrtoeol();
    refresh();
    endwin();
    printf("\033[2J\033[H\033[?25h\033[0m");
    fflush(stdout);
    exit(0);
}

static void render_art(int x, int y, int w, int h) {
    DBG("ART_RENDER: Attempting render at (%d,%d) size %dx%d", x, y, w, h);

    if (w < 10 || h < 5) {
        DBG("ART_SKIP: Box too small to render");
        return;
    }
    
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) {
        DBG("ART_ERROR: No valid art source found");
        return;
    }

    char decoded_path[4096];
    char *raw_path = (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url;
    url_decode(decoded_path, raw_path);
    
    if (access(decoded_path, F_OK) != 0) {
        DBG("ART_ERROR: File not accessible: %s", decoded_path);
        return;
    }

    char *safe_path = sh_escape(decoded_path);
    char cmd[16384];
    
    snprintf(cmd, sizeof(cmd), 
        "(ffmpeg -hide_banner -loglevel error -i \"%s\" -vframes 1 -f mjpeg - 2>/dev/null) | "
        "magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --height=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'", 
        safe_path, w*2, h*2, w*2, h*2, w - 1, h, x + 1);

    DBG("ART_CMD: %s", cmd);

    def_prog_mode(); 
    printf("\x1b[%d;%dH", y + 1, x + 1);
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        DBG("UI_ERROR: popen failed for art command");
        reset_prog_mode();
        return;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
    pclose(fp);
    
    printf("\x1b[0m");
    fflush(stdout);
    reset_prog_mode();
    DBG("ART_RENDER: Success");
}

static WINDOW *main_win = NULL;
static PANEL  *main_panel = NULL;
static WINDOW *lyr_sub = NULL;

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char rendered_path_cache[4096] = {0};
    static int last_x = 0, last_y = 0;
    
    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool needs_full_rebuild = (strcmp(current_source, rendered_path_cache) != 0) || 
                               (max_x != last_x || max_y != last_y) || resize_pending;

    // 1. GEOMETRY
    const int meta_h = 6;
    int art_x, art_y, art_w, art_h;
    int lx, ly, lw, lh;
    bool horizontal = (max_x > 90);

    // We define a "Safe Area" that is 2 rows/cols smaller than the terminal
    // to prevent the cursor from ever hitting the "Scroll Trigger" edge.
    int safe_h = max_y - 2;
    int safe_w = max_x - 2;

    if (!horizontal) {
        art_w = safe_w - 8; art_x = 4; art_y = meta_h + 1;
        art_h = (int)(art_w / (get_image_aspect(current_source) * 2.1));
        if (art_h > (safe_h * 0.40)) art_h = (safe_h * 0.40);
        lx = 4; lw = art_w; ly = art_y + art_h + 1; lh = safe_h - ly;
    } else {
        int split = (safe_w * 0.40);
        art_x = 2; art_y = meta_h + 1; art_w = split - 4;
        art_h = (int)(art_w / (get_image_aspect(current_source) * 2.1));
        if (art_h > (safe_h - art_y - 2)) art_h = (safe_h - art_y - 2);
        lx = split + 2; ly = 1; lw = safe_w - lx; lh = safe_h - 1;
    }

    // 2. PANEL REBUILD
    if (needs_full_rebuild) {
        resize_pending = false;
        last_x = max_x; last_y = max_y;
        strncpy(rendered_path_cache, current_source, 4095);

        // Cleanup old panel architecture
        if (main_panel) { del_panel(main_panel); main_panel = NULL; }
        if (main_win) { delwin(main_win); main_win = NULL; }
        if (lyr_sub) { delwin(lyr_sub); lyr_sub = NULL; }

        // Create a Window that covers the SAFE AREA
        main_win = newwin(safe_h, safe_w, 1, 1);
        main_panel = new_panel(main_win);
        
        // Create a sub-window specifically for lyrics inside main_win
        lyr_sub = subwin(main_win, lh, lw, ly, lx);
        scrollok(lyr_sub, FALSE);

        erase(); 
        wnoutrefresh(stdscr); // Keep background clean
        
        // Draw static elements to main_win (0,0 is now the safe area top-left)
        wattron(main_win, A_BOLD | COLOR_PAIR(1));
        mvwprintw(main_win, 0, 1, "[%s] %s", safe_str(m.status), safe_str(m.player));
        wattroff(main_win, A_BOLD);
        
        mvwprintw(main_win, 1, 1, "Title: %.*s", art_w, safe_str(m.title));
        wattron(main_win, COLOR_PAIR(2));
        mvwprintw(main_win, 2, 1, "Artist: %.*s", art_w, safe_str(m.artist));
        mvwprintw(main_win, 3, 1, "Album:  %.*s", art_w, safe_str(m.album));
        wattroff(main_win, COLOR_PAIR(2));

        if (horizontal) {
            for (int i = 0; i < safe_h; i++) mvwaddch(main_win, i, lx - 2, ACS_VLINE);
        }
        
        mvwprintw(main_win, safe_h - 1, 1, "Scroll: j/k | Q: Quit");
        last_scroll_pos = -1;
    }


    if (scroll_pos != last_scroll_pos || needs_full_rebuild) {
        // A. Calculate Bounds
        int max_scroll = (m.lyric_count > lh) ? (m.lyric_count - lh) : 0;
        if (scroll_pos < 0) scroll_pos = 0;
        if (scroll_pos > max_scroll) scroll_pos = max_scroll;

        // B. Clear the SUB-WINDOW (not the whole panel)
        werase(lyr_sub);
        
        // C. Re-draw the "Viewport"
        wattron(lyr_sub, COLOR_PAIR(3));
        for (int i = 0; i < lh; i++) {
            int lyric_idx = i + scroll_pos;
            if (lyric_idx < m.lyric_count && m.lyrics[lyric_idx]) {
                // Draw relative to the sub-window (0, i)
                mvwaddnstr(lyr_sub, i, 0, m.lyrics[lyric_idx], lw);
            }
        }
        wattroff(lyr_sub, COLOR_PAIR(3));

        // D. Commit changes to the Panel Stack
        // This is the step that makes Panels "notice" the sub-window update
        last_scroll_pos = scroll_pos;
        
        update_panels(); // Refreshes the Z-order and virtual screen
        doupdate();      // Actually pushes the bits to the terminal

        // E. Prevent the "Terminal Nudge"
        printf("\033[H");
        fflush(stdout);
    }

    // 4. THE ART (Only on full rebuild to avoid lag)
    if (needs_full_rebuild) {
        render_art(art_x + 1, art_y + 1, art_w, art_h);
    }
}
