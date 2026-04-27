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

    /* * ALGORITHM IMPROVEMENTS:
     * 1. magick mpr:img -set option:avg "%%[pixel:p{0,0}]" 
     * Samples the top-left pixel (0,0) to determine the background color.
     * 2. -background "%%[avg]" -extent
     * Uses that sampled color to fill the gaps created by "zooming out".
     */
    snprintf(cmd, sizeof(cmd), 
            "(ffmpeg -hide_banner -loglevel error -i \"%s\" -vframes 1 -f mjpeg - 2>/dev/null) | "
            "magick - -fuzz 15%% -trim +repage "
            "-write mpr:img -delete 0 "
            "mpr:img -set option:avg '%%[pixel:p{0,0}]' "
            "-background '%%[avg]' -bordercolor '%%[avg]' -border 5%% "
            /* We use (int)(w * 1.5) to calculate the intermediate pixel width */
            "-thumbnail %dx%d! -gravity center -extent %dx%d webp:- | " 
            "jp2a -b --width=%d --height=%d --color-depth=24 - | sed 's/^/\\x1b[%dG/'", 
            safe_path, 
            (int)(w * 1.5), h,
            (int)(w * 1.5), h,
            w, h, x + 1);

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
    static char rendered_title_cache[2048] = {0};
    static int last_x = 0, last_y = 0;
    static long last_art_render_ms = 0;
    static bool last_player_state = false;

    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status[0] != '\0' && strcmp(m.status, "Stopped") != 0);

    char current_title_sig[2048];
    snprintf(current_title_sig, sizeof(current_title_sig), "%s%s", 
             safe_str(m.title), safe_str(m.artist));
    
    bool structural_change = (max_x != last_x || max_y != last_y) || resize_pending;
    bool metadata_change = (strcmp(current_source, rendered_path_cache) != 0) || 
                           (strcmp(current_title_sig, rendered_title_cache) != 0);
    bool state_flip = (player_active != last_player_state);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    // 1. REBUILD WINDOWS ONLY ON STRUCTURAL CHANGE
    if (structural_change || state_flip || !main_win) {
        resize_pending = false;
        last_x = max_x; last_y = max_y;
        last_player_state = player_active;
        
        if (main_panel) del_panel(main_panel);
        if (main_win) delwin(main_win);
        if (lyr_sub) delwin(lyr_sub);
        if (meta_sub) delwin(meta_sub);

        main_win = newwin(max_y - 2, max_x - 2, 1, 1);
        main_panel = new_panel(main_win);
        
        printf("\033[2J\033[H"); 
        fflush(stdout);
        erase();
    }

    // 2. ALWAYS UPDATE CONTENT (Regardless of Cache)
    // This ensures that if render_art wipes the screen, ncurses puts the text back
    werase(main_win);
    if (!player_active) {
        const char* msg = "Awaiting Player Input...";
        mvwprintw(main_win, (max_y-2)/2, (max_x-2-strlen(msg))/2, "%s", msg);
        memset(rendered_path_cache, 0, sizeof(rendered_path_cache));
    } else {
        int safe_w = max_x - 2;
        int safe_h = max_y - 2;
        bool horizontal = (max_x > 90);
        int art_w = horizontal ? (int)(safe_w * 0.45) : safe_w - 6;
        int art_h = (int)(art_w * 0.5);

        // Re-derive subwindows if they were lost or need relocation
        if (!meta_sub || !lyr_sub || structural_change) {
            if (meta_sub) delwin(meta_sub);
            if (lyr_sub) delwin(lyr_sub);
            if (!horizontal) {
                meta_sub = derwin(main_win, 5, safe_w - 4, art_h + 1, 2);
                lyr_sub = derwin(main_win, safe_h - art_h - 8, safe_w - 4, art_h + 7, 2);
            } else {
                int split = art_w + 4;
                meta_sub = derwin(main_win, 6, split - 4, art_h + 1, 2);
                lyr_sub = derwin(main_win, safe_h, safe_w - split - 2, 0, split + 2);
            }
        }

        // Draw Metadata
        werase(meta_sub);
        wattron(meta_sub, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(meta_sub, 0, 0, "● %s", safe_str(m.status));
        wattroff(meta_sub, A_BOLD);
        mvwprintw(meta_sub, 1, 0, "Title:  %s", safe_str(m.title));
        wattron(meta_sub, COLOR_PAIR(2));
        mvwprintw(meta_sub, 2, 0, "Artist: %s", safe_str(m.artist));
        mvwprintw(meta_sub, 3, 0, "Album:  %s", safe_str(m.album));
        wattroff(meta_sub, COLOR_PAIR(2));

        // Draw Lyrics or Fallback
        if (metadata_change) scroll_pos = 0;
        werase(lyr_sub);
        if (m.lyric_count > 0) {
            wattron(lyr_sub, COLOR_PAIR(3));
            for (int i = 0; i < getmaxy(lyr_sub) && (i + scroll_pos) < m.lyric_count; i++) {
                mvwaddnstr(lyr_sub, i, 0, m.lyrics[i + scroll_pos], getmaxx(lyr_sub));
            }
            wattroff(lyr_sub, COLOR_PAIR(3));
        } else {
            wattron(lyr_sub, A_DIM);
            mvwprintw(lyr_sub, 2, 0, "  (No lyrics found for this track)");
            wattroff(lyr_sub, A_DIM);
        }
    }

    // 3. FINAL PUSH
    strncpy(rendered_title_cache, current_title_sig, sizeof(rendered_title_cache) - 1);
    touchwin(stdscr);
    update_panels();
    doupdate();

    // 4. ART (Drawn over the ncurses buffer)
    if (player_active && strlen(current_source) > 5) {
        if (metadata_change || structural_change || (now_ms - last_art_render_ms > 200)) {
            int art_w = (max_x > 90) ? (int)((max_x - 2) * 0.45) : (max_x - 8);
            int art_h = (int)(art_w * 0.5);
            render_art(3, 1, art_w, art_h);
            last_art_render_ms = now_ms;
            strncpy(rendered_path_cache, current_source, sizeof(rendered_path_cache) - 1);
        }
    }
}
