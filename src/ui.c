#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "metadata.h"
#include "utils.h"

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
    
    char *source = (strncmp(m.url, "file://", 7) == 0) ? m.url + 7 : m.url;
    if (access(source, F_OK) != 0) {
        last_art_path[0] = '\0';
        return;
    }

    if (!force && strcmp(source, last_art_path) == 0 && w == last_w && h == last_h) return;

    char cmd[16384];
    char *safe_path = sh_escape(source);
    
    /*
     * 1. ffmpeg: tries to extract attached picture. We removed '-c copy' to force 
     * a re-decode to mjpeg if the internal format is weird (fixes decode delegate error).
     * 2. magick: converts to webp for jp2a.
     * 3. jp2a: generates the colored ANSI.
     * 4. sed: Prepends the ANSI horizontal jump code to EVERY line of art.
     */
    snprintf(cmd, sizeof(cmd), 
        "(ffmpeg -hide_banner -loglevel error -i \"%s\" -map 0:v:0 -f mjpeg - || cat \"%s\") 2>/dev/null | "
        "magick - -thumbnail %dx%d^ -gravity center -extent %dx%d webp:- | "
        "jp2a -b --width=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'",
        safe_path, safe_path, w*2, h*2, w*2, h*2, w, x + 1);

    // Explicitly move the physical terminal cursor to our starting Y, column 1
    printf("\x1b[%d;1H", y + 1); 
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            fputs(line, stdout); // sed handles the horizontal X offset per line
        }
        pclose(fp);
        strncpy(last_art_path, source, 4095);
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
