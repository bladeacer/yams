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
    
    char *source = NULL;
    // Check path first
    if (strlen(m.file_path) > 5) {
        char *p = (strncmp(m.file_path, "file://", 7) == 0) ? m.file_path + 7 : m.file_path;
        if (access(p, F_OK) == 0) source = p; 
    } 

    // If no local file, don't attempt to render
    if (!source) return;

    static char last_source[4096] = {0};
    if (strcmp(source, last_source) == 0) return;

    // Use ffmpeg to extract the stream directly into the image pipeline
    char cmd[16384];
    char *safe_path = sh_escape(source);
    snprintf(cmd, sizeof(cmd), 
        "ffmpeg -loglevel error -i '%s' -map 0:v -f image2 pipe:1 2>/dev/null | "
        "magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'",
        safe_path, w*2, h*2, w*2, h*2, w, x + 1);

    strncpy(last_source, source, 4095);

    move(y, x); refresh();
    printf("\x1b[%d;%dH", y + 1, x + 1);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) fputs(line, stdout);
        pclose(fp);
    }
    fflush(stdout);
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    if (max_y < 10 || max_x < 40) return;

    int split = max_x / 2;
    int lyric_area_h = max_y - 2;

    erase();

    // --- LEFT SIDE: Static Metadata & Keybinds ---
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(1, 2, "[%s] %s", m.status, m.player);
    
    // We use move + addstr to avoid byte-length issues with CJK in mvwprintw
    move(2, 2);
    addstr("Title: ");
    addstr(m.title); 
    attroff(A_BOLD);

    attron(COLOR_PAIR(2));
    mvprintw(3, 2, "Artist: %s", m.artist);
    mvprintw(4, 2, "Album:  %s", m.album);
    attroff(COLOR_PAIR(2));

    // Keybind Indicators (pinned to bottom left)
    attron(A_DIM);
    mvprintw(max_y - 3, 2, "k / ↑ : Scroll Up");
    mvprintw(max_y - 2, 2, "j / ↓ : Scroll Down");
    attroff(A_DIM);

    // Vertical Divider
    for (int y = 1; y < max_y - 1; y++) mvaddch(y, split, ACS_VLINE);

    // --- RIGHT SIDE: Scrollable Lyrics ---
    attron(COLOR_PAIR(3));

    // Fix Scrolling Bounds
    if (m.lyric_count <= lyric_area_h) {
        scroll_pos = 0;
    } else {
        if (scroll_pos < 0) scroll_pos = 0;
        if (scroll_pos > m.lyric_count - lyric_area_h) 
            scroll_pos = m.lyric_count - lyric_area_h;
    }

    for (int i = 0; i < lyric_area_h; i++) {
        int idx = i + scroll_pos; // scroll_pos only shifts the lyric index
        if (idx < m.lyric_count && m.lyrics[idx]) {
            move(i + 1, split + 2);
            
            // addstr is UTF-8 aware. We don't manually truncate 
            // to avoid cutting a 3-byte CJK character in half.
            addstr(m.lyrics[idx]); 
            
            // Wipe any artifacts to the right of the text
            clrtoeol(); 
        }
    }
    attroff(COLOR_PAIR(3));

    refresh();
    
    // Render Art (Static position on left side)
    if (!resize_pending) render_art(2, 6, split - 5, max_y - 11);
}
