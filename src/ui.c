#include <ncurses.h>
#include <wchar.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"

bool resize_pending = false;
int scroll_pos = 0;
char last_art_url[4096] = {0};

void init_ui() {
    initscr(); start_color(); use_default_colors();
    curs_set(0); noecho(); keypad(stdscr, TRUE); timeout(20);
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, 250, -1);
}

void cleanup_ui(int sig) {
    (void)sig;
    endwin();
    printf("\033[?25h\033[0m");
    exit(0);
}

static void render_art(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    char *source_raw = NULL;
    int is_file = 0;

    if (strlen(m.file_path) > 5) {
        char *p = (strncmp(m.file_path, "file://", 7) == 0) ? m.file_path + 7 : m.file_path;
        if (access(p, F_OK) == 0) { source_raw = p; is_file = 1; }
    }
    if (!source_raw && strlen(m.art_url) > 5) {
        char *p = (strncmp(m.art_url, "file://", 7) == 0) ? m.art_url + 7 : m.art_url;
        if (access(p, F_OK) == 0) { source_raw = p; is_file = 1; }
        else if (strncmp(m.art_url, "http", 4) == 0) { source_raw = m.art_url; is_file = 0; }
    }

    static int last_w = 0, last_h = 0;
    if (source_raw && strcmp(source_raw, last_art_url) == 0 && w == last_w && h == last_h) return;

    char cmd[16384];
    if (source_raw) {
        char *safe_path = sh_escape(source_raw);
        snprintf(cmd, sizeof(cmd), is_file ? 
            "ffmpeg -loglevel error -i '%s' -map 0:v -f image2 pipe:1 | magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | jp2a --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'" :
            "magick '%s' -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | jp2a --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'",
            safe_path, w*2, h*2, w*2, h*2, w, x + 1);
        strncpy(last_art_url, source_raw, 4095);
    } else {
        snprintf(cmd, sizeof(cmd), "magick -size %dx%d canvas:black webp:- | jp2a --width=%d - | sed 's/^/\\x1b[%dG/'", w*10, h*10, w, x+1);
        last_art_url[0] = '\0';
    }

    move(y, x); refresh();
    printf("\x1b[%d;%dH", y + 1, x + 1);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
        pclose(fp);
        last_w = w; last_h = h;
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int split = max_x / 2;

    erase();
    if (strlen(m.title) == 0) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(max_y/2, (max_x/2)-8, "YAMS : STANDBY");
        attroff(A_BOLD | COLOR_PAIR(1));
        refresh(); return;
    }

    // --- Left Side Info ---
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", m.status, m.player);
    
    // Safely draw Title (handles CJK width)
    move(2, 2);
    // mvwaddnstr is better for UTF-8 than printf truncation
    mvwaddnstr(stdscr, 2, 2, m.title, split - 4); 
    attroff(A_BOLD);
    
    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist: ");
    mvwaddnstr(stdscr, 3, 10, m.artist, split - 12);
    
    mvprintw(4, 2, "Album:  ");
    mvwaddnstr(stdscr, 4, 10, m.album, split - 12);
    attroff(COLOR_PAIR(2));

    // --- Right Side Lyrics ---
    attron(COLOR_PAIR(3));
    int lyric_area_h = max_y - 2;

    // Scrolling boundary protection
    if (m.lyric_count > lyric_area_h) {
        if (scroll_pos > m.lyric_count - lyric_area_h)
            scroll_pos = m.lyric_count - lyric_area_h;
    } else {
        scroll_pos = 0;
    }

    for (int i = 0; i < lyric_area_h && (i + scroll_pos) < m.lyric_count; i++) {
        // Use mvwaddnstr to prevent CJK truncation artifacts
        mvwaddnstr(stdscr, i + 1, split + 2, m.lyrics[i + scroll_pos], max_x - split - 4);
    }
    attroff(COLOR_PAIR(3));

    refresh();
    if (!resize_pending) render_art(2, 9, split - 6, max_y - 11);
}
