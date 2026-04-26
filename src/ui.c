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
    timeout(20);
}

void cleanup_ui(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h\033[0m");
    exit(0);
}

static void render_art(int x, int y, int w, int h, bool force) {
    if (w <= 0 || h <= 0 || strlen(m.url) < 5) return;

    char decoded_path[4096];
    // 1. Strip file:// prefix
    char *raw_path = (strncmp(m.url, "file://", 7) == 0) ? m.url + 7 : m.url;
    
    // 2. Decode %20 and other URL entities
    url_decode(decoded_path, raw_path);

    // 3. Telemetry: Check if the file is actually reachable now
    if (access(decoded_path, F_OK) != 0) {
        DBG("ART_ERR: File not found after decode: %s", decoded_path);
        last_art_path[0] = '\0';
        return;
    }

    if (!force && strcmp(decoded_path, last_art_path) == 0 && w == last_w && h == last_h) return;

    char cmd[16384];
    char *safe_path = sh_escape(decoded_path);
    
    // The "Golden" Pipeline
    snprintf(cmd, sizeof(cmd), 
        "(ffmpeg -hide_banner -loglevel error -i \"%s\" -map 0:v:0 -vframes 1 -f mjpeg - || head -c 1M \"%s\") 2>/dev/null | "
        "magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'",
        safe_path, safe_path, w*2, h*2, w*2, h*2, w, x + 1);

    DBG("ART_EXEC: %s", decoded_path);

    // Draw logic
    printf("\x1b[%d;1H", y + 1);
    fflush(stdout);

    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout);
        }
        pclose(fp);
        strncpy(last_art_path, decoded_path, 4095);
        last_w = w; last_h = h;
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    if (max_y < 10 || max_x < 40) return;

    erase();

    if (m.title[0] == '\0') {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(max_y / 2, (max_x - 14) / 2, "YAMS : STANDBY");
        attroff(A_BOLD | COLOR_PAIR(1));
        refresh();
        return;
    }

    int split = max_x / 2;
    int lyric_area_h = max_y - 2;

    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", m.status, m.player);
    mvwaddnstr(stdscr, 2, 2, m.title, split - 4);
    attroff(A_BOLD);

    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist: %s", m.artist);
    mvprintw(4, 2, "Album:  %s", m.album);
    attroff(COLOR_PAIR(2));

    for (int y = 1; y < max_y - 1; y++) mvaddch(y, split, ACS_VLINE);

    if (m.lyric_count > 0) {
        if (scroll_pos > m.lyric_count - lyric_area_h) scroll_pos = m.lyric_count - lyric_area_h;
        if (scroll_pos < 0) scroll_pos = 0;
        attron(COLOR_PAIR(3));
        for (int i = 0; i < lyric_area_h && (i + scroll_pos) < m.lyric_count; i++) {
            mvwaddnstr(stdscr, i + 1, split + 2, m.lyrics[i + scroll_pos], max_x - split - 4);
        }
        attroff(COLOR_PAIR(3));
    }

    // Refresh text FIRST
    refresh();
    
    // Give ncurses a microsecond to breathe
    fflush(stdout); 

    if (!resize_pending) {
        // Now that ncurses is done writing its frame, we "paint" our art on top
        render_art(2, 6, split - 5, max_y - 8, true);
    }
}
