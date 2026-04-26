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
char last_art_path[4096] = {0};
static int last_w = 0, last_h = 0;

void init_ui() {
    initscr();
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
    timeout(20); // Faster timeout to catch resize events quickly
}

void cleanup_ui(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h\033[0m");
    exit(0);
}

static void render_art(int x, int y, int w, int h, bool force) {
    if (w <= 0 || h <= 0 || !force) return;

    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (strlen(input_url) < 5) return;

    char decoded_path[4096];
    char *raw_path = (strncmp(input_url, "file://", 7) == 0) ? input_url + 7 : input_url;
    url_decode(decoded_path, raw_path);

    if (access(decoded_path, F_OK) != 0) return;

    // Dynamic Fuzz: Lower fuzz for smaller images to avoid eating detail
    int fuzz_percent = (w < 40) ? 2 : 10;
    
    char cmd[16384];
    char *safe_path = sh_escape(decoded_path);
    
    snprintf(cmd, sizeof(cmd), 
        "(ffmpeg -hide_banner -loglevel error -i \"%s\" -map 0:v:0 -vframes 1 -f mjpeg - || head -c 1M \"%s\") 2>/dev/null | "
        "magick - -fuzz %d%% -trim +repage -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'",
        safe_path, safe_path, fuzz_percent, w*2, h*2, w*2, h*2, w, x + 1);

    printf("\x1b[%d;1H", y + 1); 
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
        pclose(fp);
        strncpy(last_art_path, decoded_path, 4095);
        last_w = w; last_h = h;
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    static char art_url_cache[4096] = {0};
    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool path_changed = (strcmp(current_source, art_url_cache) != 0);
    
    // Total clear on resize or track change
    if (path_changed || resize_pending) {
        erase();
        strncpy(art_url_cache, current_source, 4095);
    }

    if (m.title[0] == '\0') {
        mvprintw(max_y / 2, (max_x - 14) / 2, "YAMS : STANDBY");
        refresh();
        return;
    }

    bool vertical_layout = (max_x < 100); 
    int split_x = vertical_layout ? max_x : max_x / 2;
    int lyric_start_y, lyric_h, art_w, art_h, art_y;

    if (vertical_layout) {
        // TOP/BOTTOM SPLIT
        art_y = 6;
        // Limit art to at most 80% of total height, leaving room for metadata and help
        int max_art_h = (max_y * 8) / 10; 
        art_h = max_art_h - 8; 
        if (art_h < 4) art_h = 4; // Sanity check

        art_w = max_x - 4;
        lyric_start_y = art_y + art_h + 1;
        lyric_h = max_y - lyric_start_y - 1;
    } else {
        // LEFT/RIGHT SPLIT
        art_y = 6;
        art_h = max_y - 9;
        art_w = split_x - 5;
        lyric_start_y = 1;
        lyric_h = max_y - 2;
        for (int y = 1; y < max_y - 1; y++) mvaddch(y, split_x, ACS_VLINE);
    }

    // --- TEXT RENDERING ---
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", m.status, m.player);
    mvwaddnstr(stdscr, 2, 2, m.title, split_x - 4);
    attroff(A_BOLD);
    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist: %s", m.artist);
    mvprintw(4, 2, "Album:  %s", m.album);
    attroff(COLOR_PAIR(2));
    mvprintw(max_y - 1, 2, "Scroll: j/k | Quit: q");

    // Clear and draw lyrics
    for(int i=0; i < lyric_h; i++) {
        move(lyric_start_y + i, vertical_layout ? 0 : split_x + 1);
        clrtoeol();
    }

    if (m.lyric_count > 0) {
        int max_scroll = (m.lyric_count > lyric_h) ? (m.lyric_count - lyric_h) : 0;
        if (scroll_pos > max_scroll) scroll_pos = max_scroll;
        if (scroll_pos < 0) scroll_pos = 0;

        attron(COLOR_PAIR(3));
        for (int i = 0; i < lyric_h && (i + scroll_pos) < m.lyric_count; i++) {
            mvwaddnstr(stdscr, lyric_start_y + i, vertical_layout ? 2 : split_x + 2, 
                       m.lyrics[i + scroll_pos], max_x - (vertical_layout ? 4 : split_x + 4));
        }
        attroff(COLOR_PAIR(3));
    }

    refresh();
    fflush(stdout);

    // Only render the image if triggered
    if (path_changed || resize_pending) {
        render_art(2, art_y, art_w, art_h, true);
        resize_pending = false; 
    }
}
