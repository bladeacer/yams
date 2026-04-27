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
    const char *raw_path = input_url;
    if (strncmp(input_url, "file://", 7) == 0) {
        raw_path = input_url + 7;
    }
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

static void format_display_url(char *dest, const char *src, size_t size) {
    if (!src || strlen(src) == 0) {
        strncpy(dest, "N/A", size);
        return;
    }

    char decoded[4096];
    const char *path_start = (strncmp(src, "file://", 7) == 0) ? src + 7 : src;
    url_decode(decoded, path_start);

    const char *home = getenv("HOME");
    if (home && strncmp(decoded, home, strlen(home)) == 0) {
        snprintf(dest, size, "~%s", decoded + strlen(home));
    } else {
        strncpy(dest, decoded, size - 1);
    }
}

// Move help text to a helper so it's consistent
static void draw_help_text(WINDOW *win, int y, int x) {
    wattron(win, A_REVERSE | A_BOLD);
    mvwprintw(win, y, x, " q "); 
    wattroff(win, A_REVERSE | A_BOLD);
    waddstr(win, " Quit  ");
    wattron(win, A_REVERSE | A_BOLD);
    waddstr(win, " r ");
    wattroff(win, A_REVERSE | A_BOLD);
    waddstr(win, " Refresh");
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char rendered_path_cache[4096] = {0};
    static char rendered_title_cache[2048] = {0};
    static int last_x = 0, last_y = 0;
    static long last_art_render_ms = 0;
    static bool last_player_state = false;

    if (resize_pending) {
        memset(rendered_path_cache, 0, sizeof(rendered_path_cache));
        memset(rendered_title_cache, 0, sizeof(rendered_title_cache));
        last_art_render_ms = 0;
        printf("\033[2J\033[H\033[0m"); 
        fflush(stdout);
        clearok(stdscr, TRUE); 
    }

    char *current_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status[0] != '\0' && strcmp(m.status, "Stopped") != 0);
    char current_title_sig[2048];
    snprintf(current_title_sig, sizeof(current_title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));
    
    bool structural_change = (max_x != last_x || max_y != last_y) || resize_pending;
    bool metadata_change = (strcmp(current_source, rendered_path_cache) != 0) || 
                           (strcmp(current_title_sig, rendered_title_cache) != 0);
    bool state_flip = (player_active != last_player_state);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    if (structural_change || state_flip || !main_win) {

        // 1. Reset caches so art/metadata re-renders immediately
        memset(rendered_path_cache, 0, sizeof(rendered_path_cache));
        memset(rendered_title_cache, 0, sizeof(rendered_title_cache));
        last_art_render_ms = 0;

        // 2. Hardware Clear (Wipes the actual terminal emulator's buffer)
        // This is the "Ghost Buster" line.
        printf("\033[2J\033[H\033[0m"); 
        fflush(stdout);

        // 3. Logic Reset
        resize_pending = false; 
        last_x = max_x; last_y = max_y;
        last_player_state = player_active;

        // 4. Window Reconstruction
        if (main_panel) { del_panel(main_panel); main_panel = NULL; }
        if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
        if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
        if (main_win)   { delwin(main_win);   main_win = NULL; }

        main_win = newwin(max_y - 2, max_x - 2, 1, 1);
        main_panel = new_panel(main_win);
        
        // 5. Ncurses Buffer Reset
        erase(); 
        clearok(stdscr, TRUE);
        refresh();
    }

    werase(main_win);
    if (!player_active) {
        const char* msg = "Awaiting Player Input...";
        mvwprintw(main_win, (max_y-2)/2, (max_x-2-strlen(msg))/2, "%s", msg);
        draw_help_text(main_win, (max_y-2)/2 + 2, (max_x-2-26)/2);
    } else {
        int safe_w = max_x - 2;
        int safe_h = max_y - 2;
        bool horizontal = (max_x > 90);
        
        // --- HARD LIMITS FOR ULTRAWIDE ---
        int max_allowed_w = horizontal ? 54 : 50; // Slightly narrower for ultrawide
        int max_allowed_h = horizontal ? 24 : 22; // Hard cap on height to save metadata space

        int art_w = horizontal ? (int)(safe_w * 0.4) : safe_w - 6;
        if (art_w > max_allowed_w) art_w = max_allowed_w;

        int art_h = (int)(art_w * 0.5); 
        if (art_h > max_allowed_h) art_h = max_allowed_h;

        // Re-derive subwindows
        if (!meta_sub || !lyr_sub) {
            if (horizontal) {
                // Fix: Ensure split doesn't push lyrics too far or metadata too wide
                int split = art_w + 6; 
                int meta_w = split - 4;
                if (meta_w > 60) meta_w = 60; // Cap metadata width

                meta_sub = derwin(main_win, 11, meta_w, art_h + 2, 2);
                lyr_sub = derwin(main_win, safe_h, safe_w - split - 2, 0, split + 2);
            } else {
                meta_sub = derwin(main_win, 11, safe_w - 4, art_h + 2, 2);
                lyr_sub = derwin(main_win, safe_h - art_h - 15, safe_w - 4, art_h + 14, 2);
            }
        }

        // Render Metadata
        werase(meta_sub);
        wattron(meta_sub, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(meta_sub, 0, 0, "  Player:   %s | %s", safe_str(m.player), safe_str(m.status));
        mvwprintw(meta_sub, 1, 0, "  Duration: %s ", m.length_str);
        wattroff(meta_sub, A_BOLD);
        mvwprintw(meta_sub, 2, 0, "  Title:    %s", safe_str(m.title));
        wattron(meta_sub, COLOR_PAIR(2));
        mvwprintw(meta_sub, 3, 0, "  Artist:   %s", safe_str(m.artist));
        mvwprintw(meta_sub, 4, 0, "  Album:    %s", safe_str(m.album));
        wattroff(meta_sub, COLOR_PAIR(2));
        mvwprintw(meta_sub, 5, 0, "  Genre:    %s", safe_str(m.genre));
        mvwprintw(meta_sub, 6, 0, "  Composer: %s", safe_str(m.composer));
        wattron(meta_sub, A_DIM);
        char display_url[1024];
        format_display_url(display_url, m.file_path, sizeof(display_url));
        mvwprintw(meta_sub, 7, 0, "  Source:   %s", display_url);
        wattroff(meta_sub, A_DIM);
        draw_help_text(meta_sub, 10, 0);

        // Render Lyrics
        werase(lyr_sub);
        if (m.lyric_count > 0) {
            wattron(lyr_sub, COLOR_PAIR(3));
            for (int i = 0; i < getmaxy(lyr_sub) && i < m.lyric_count; i++) {
                mvwaddnstr(lyr_sub, i, 0, m.lyrics[i], getmaxx(lyr_sub));
            }
            wattroff(lyr_sub, COLOR_PAIR(3));
        } else {
            wattron(lyr_sub, A_DIM);
            mvwprintw(lyr_sub, 2, 0, "  (No lyrics found)");
            wattroff(lyr_sub, A_DIM);
        }

        // 5. THE IMAGE
        if (player_active && (metadata_change || structural_change || (now_ms - last_art_render_ms > 200))) {
            render_art(3, 1, art_w, art_h);
            last_art_render_ms = now_ms;
            strncpy(rendered_path_cache, current_source, sizeof(rendered_path_cache) - 1);
        }
    }

    strncpy(rendered_title_cache, current_title_sig, sizeof(rendered_title_cache) - 1);
    update_panels();
    doupdate();
}
