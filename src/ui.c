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
WINDOW *lyric_win = NULL;
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

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char rendered_path_cache[4096] = {0};
    static int last_x = 0, last_y = 0;
    
    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    
    // 1. DIRECT COMPARISON
    bool path_changed = (strcmp(current_source, rendered_path_cache) != 0);
    bool size_changed = (max_x != last_x || max_y != last_y);
    
    // 2. THE REBUILD CHECK
    // Note: We check resize_pending, then IMMEDIATELY clear it 
    // so it can't loop even if it's set globally.
    bool force_resize = resize_pending;
    resize_pending = false; 

    bool needs_full_rebuild = path_changed || size_changed || force_resize;

    if (needs_full_rebuild) {
        // Log exactly why we are wiping the screen
        DBG("UI_WIPE_TRIGGER: Path:%d Size:%d Signal:%d Source:%s", 
             path_changed, size_changed, force_resize, current_source);

        last_x = max_x;
        last_y = max_y;
        strncpy(rendered_path_cache, current_source, 4095);
        
        // Pure ncurses erase is safer here than ANSI escapes
        erase();
    }

    // --- 3. GEOMETRY ---
    const int meta_h = 6;
    int art_x, art_y, art_w, art_h;
    int lyr_x, lyr_y, lyr_w, lyr_h;
    
    float img_aspect = get_image_aspect(current_source);
    float term_adj_aspect = (img_aspect > 0.1) ? img_aspect * 2.1 : 2.1;
    bool vertical = (max_x < 100);

    if (vertical) {
        art_x = 4; art_y = meta_h + 2;
        art_w = max_x - 8;
        art_h = (int)(art_w / term_adj_aspect);
        if (art_h > (max_y * 0.30)) art_h = (max_y * 0.30);
        lyr_x = 2; lyr_y = art_y + art_h + 1;
        lyr_w = max_x - 4; lyr_h = max_y - lyr_y - 1;
    } else {
        int split_x = (max_x * 0.45); 
        art_x = 2; art_y = meta_h + 2;
        art_w = split_x - 4;
        art_h = (int)(art_w / term_adj_aspect);
        if (art_h > (max_y - art_y - 2)) art_h = (max_y - art_y - 2);
        lyr_x = split_x + 2; lyr_y = 1;
        lyr_w = max_x - lyr_x - 2; lyr_h = max_y - 2;
        for (int i = 1; i < max_y - 1; i++) mvaddch(i, split_x, ACS_VLINE);
    }

    // --- 4. RENDER TEXT ---
    // We do NOT call clrtoeol() here because we already erased if needed.
    // Calling clrtoeol() constantly can also flicker.
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", safe_str(m.status), safe_str(m.player));
    mvprintw(2, 2, "Title: %.*s", (vertical ? max_x : lyr_x) - 10, safe_str(m.title));
    attroff(A_BOLD);
    
    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist: %.*s", (vertical ? max_x : lyr_x) - 10, safe_str(m.artist));
    mvprintw(4, 2, "Album:  %.*s", (vertical ? max_x : lyr_x) - 10, safe_str(m.album));
    attroff(COLOR_PAIR(2));

    if (m.lyric_count > 0 && lyr_h > 0) {
        attron(COLOR_PAIR(3));
        for (int i = 0; i < lyr_h && (i + scroll_pos) < m.lyric_count; i++) {
            if (m.lyrics[i + scroll_pos])
                mvprintw(lyr_y + i, lyr_x, "%.*s", lyr_w, m.lyrics[i + scroll_pos]);
        }
        attroff(COLOR_PAIR(3));
    }

    mvprintw(max_y - 1, 2, "Scroll: j/k | Q: Quit");

    // --- 5. SYNC ---
    refresh(); 
    fflush(stdout);

    // --- 6. OVERLAY ART ---
    if (needs_full_rebuild) {
        render_art(art_x, art_y, art_w, art_h);
    }
}
