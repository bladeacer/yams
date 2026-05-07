#include <panel.h>
#include <ncurses.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"
#include "metadata.h"
#include "art.h"
#include "utils.h"
#include "debug.h"

bool resize_pending = false;

static WINDOW *main_win = NULL;
static PANEL  *main_panel = NULL;
static WINDOW *lyr_sub = NULL;
static WINDOW *meta_sub = NULL;

static int last_w = 0;
static int last_h = 0;
static char last_art_path[4096] = {0};
static char last_title_sig[2048] = {0};
static bool last_player_active = false;

void init_ui() {
    initscr();
    set_escdelay(25);
    cbreak();
    noecho();
    noqiflush();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    curs_set(0); 

    mousemask(0, NULL);
    mouseinterval(-1);
    intrflush(stdscr, FALSE);

    printf("\033[?2004l"); 
    printf("\033[?1000l\033[?1002l\033[?1003l\033[?1006l");
    fflush(stdout);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, 250, -1);
        init_pair(4, COLOR_MAGENTA, -1);
    }
    DBG("Terminal initialized: %dx%d", COLS, LINES);
}

void cleanup_ui(int sig) {
    (void)sig;
    endwin();
    printf("\033[2J\033[H\033[?25h\033[0m");
    fflush(stdout);
    exit(0);
}

static const char* safe_str(const char* s) {
    return (s && strlen(s) > 0) ? s : "Unknown";
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

static void draw_item(WINDOW *win, const char* key, const char* desc, 
        int *cy, int *cx, int x_orig, int max_h, int max_w) {
    int len = strlen(key) + strlen(desc) + 4;
    
    if (*cx + len > max_w - 1) {
        (*cy)++;
        *cx = x_orig;
    }
    
    if (*cy >= max_h) return;

    wattron(win, A_REVERSE | A_BOLD);
    mvwprintw(win, *cy, *cx, " %s ", key);
    wattroff(win, A_REVERSE | A_BOLD);
    
    wattron(win, A_BOLD);
    wprintw(win, " %s  ", desc);
    wattroff(win, A_BOLD);
    
    *cx = getcurx(win);
}

static void draw_help_text(WINDOW *win, int y, int x) {
    int cur_y = y;
    int cur_x = x;
    int max_h, max_w;
    getmaxyx(win, max_h, max_w);

    wattron(win, A_BOLD);
    draw_item(win, "q", "Quit", &cur_y, &cur_x, x, max_h, max_w);
    draw_item(win, "r", "Refresh", &cur_y, &cur_x, x, max_h, max_w);
    draw_item(win, "?", "Help", &cur_y, &cur_x, x, max_h, max_w);
    wattroff(win, A_BOLD);

    char seek_str[16];

    wattron(win, A_BOLD);
    snprintf(seek_str, sizeof(seek_str), "Interval: %ds", get_seek_interval());
    draw_item(win, "-/+", seek_str, &cur_y, &cur_x, x, max_h, max_w);
    wattroff(win, A_BOLD);

}

static void draw_fallback_screen(WINDOW *win, int max_y, int max_x) {
    int mid_y = max_y / 2;
    int mid_x = max_x / 2;

    wattron(win, A_BOLD);
    const char* logo[] = {
        "██╗   ██╗ █████╗ ███╗   ███╗███████╗",
        "╚██╗ ██╔╝██╔══██╗████╗ ████║██╔════╝",
        " ╚████╔╝ ███████║██╔████╔██║███████╗",
        "  ╚██╔╝  ██╔══██║██║╚██╔╝██║╚════██║",
        "   ██║   ██║  ██║██║ ╚═╝ ██║███████║",
        "   ╚═╝   ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝"
    };
    for(int i = 0; i < 5; i++) {
        mvwprintw(win, mid_y - 6 + i, mid_x - 19, "%s", logo[i]);
    }
    const char* msg2 = "Awaiting Player Input...";
    mvwprintw(win, mid_y + 1, mid_x - (strlen(msg2)/2), "%s", msg2);

    wattroff(win, A_BOLD);

    wattron(win, A_DIM);
    const char* msg1 = "Yet Another Media Screensaver";
    mvwprintw(win, mid_y + 2, mid_x - (strlen(msg1)/2), "%s", msg1);

    const char* repo = "https://codeberg.org/bladeacer/yams";

    mvwprintw(win, mid_y + 4, mid_x - (strlen(repo)/2), "%s", repo);
    wattroff(win, A_DIM);

    draw_help_text(win, mid_y + 6, mid_x - 24);
}

static void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    werase(win);

    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 0, 0, "Player:   %s - %s", safe_str(m.player), safe_str(m.status));
    mvwprintw(win, 1, 0, "Duration: %s ", m.length_str);
    mvwprintw(win, 2, 0, "Title:    %s", safe_str(m.title));
    wattroff(win, A_BOLD);

    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, 3, 0, "Artist:   %s", safe_str(m.artist));
    mvwprintw(win, 4, 0, "Album:    %s", safe_str(m.album));
    mvwprintw(win, 5, 0, "Genre:    %s", safe_str(m.genre));
    mvwprintw(win, 6, 0, "Composer: %s", safe_str(m.composer));
    wattroff(win, COLOR_PAIR(2));

    wattron(win, A_DIM);
    char disp_url[1024];
    format_display_url(disp_url, m.file_path, sizeof(disp_url));
    mvwprintw(win, 7, 0, "Source:   %s", disp_url);
    wattroff(win, A_DIM);

    draw_help_text(win, 11, 0);

    wnoutrefresh(win);
}

static void render_lyrics_pane(WINDOW *win) {
    if (!win) return;
    werase(win);
    
    int h, w;
    getmaxyx(win, h, w);

    if (m.lyric_count > 0) {
        wattron(win, COLOR_PAIR(3));
        for (int i = 0; i < h && i < m.lyric_count; i++) {
            if (m.lyrics[i]) {
                mvwaddnstr(win, i, 0, m.lyrics[i], w);
            }
        }
        wattroff(win, COLOR_PAIR(3));
    } else {
        wattron(win, A_DIM);
        mvwprintw(win, 2, 0, "  (No lyrics found)");
        wattroff(win, A_DIM);
    }
    wnoutrefresh(win); 
}

static void update_and_render_art(const char* current_art_source, const char* current_title_sig, 
        int art_w, int art_h, bool force) {
    
    bool source_changed = strcmp(current_art_source, last_art_path) != 0;
    bool title_changed = strcmp(current_title_sig, last_title_sig) != 0;

    if (force || source_changed || title_changed) {
        DBG("Executing Art Render: %s (Force: %s)", current_art_source, force ? "Yes" : "No");
        
        render_art(3, 1, art_w, art_h);

        strncpy(last_art_path, current_art_source, sizeof(last_art_path) - 1);
        strncpy(last_title_sig, current_title_sig, sizeof(last_title_sig) - 1);
    }
}

static void recreate_ui_components(int max_y, int max_x, 
    const char* art_src, const char* title_sig, int w, int h) {
    
    DBG("Reconstructing UI components: %dx%d", max_x, max_y);

    printf("\033[2J\033[H\033[0m"); 
    fflush(stdout);

    if (main_panel) { del_panel(main_panel); main_panel = NULL; }
    if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
    if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
    if (main_win)   { delwin(main_win);   main_win = NULL; }

    resizeterm(max_y, max_x);
    clearok(stdscr, TRUE);
    erase();

    main_win = newwin(max_y - 2, max_x - 2, 1, 1);
    main_panel = new_panel(main_win);

    refresh();

    last_art_path[0] = '\0';
    last_title_sig[0] = '\0';

    update_and_render_art(art_src, title_sig, w, h, true);

    last_w = max_x; 
    last_h = max_y;
    resize_pending = false;
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);


    bool horizontal = (max_x > 90);
    int safe_w = max_x - 3;
    int safe_h = max_y - 3;
    int art_w = horizontal ? (int)(safe_w * 0.4) : safe_w - 8;
    int max_dim = horizontal ? 54 : 50;
    if (art_w > max_dim) art_w = max_dim;
    int art_h = horizontal ? (int)(safe_h * 0.6) : safe_h - 8;

    char *current_art_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status[0] != '\0' && strcmp(m.status, "Stopped") != 0);

    char current_title_sig[2048];
    snprintf(current_title_sig, sizeof(current_title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));

    bool is_resize = (max_x != last_w || max_y != last_h || resize_pending);
    bool is_new_track = (strcmp(current_art_source, last_art_path) != 0) || 
        (strcmp(current_title_sig, last_title_sig) != 0);
    bool state_flipped = (player_active != last_player_active);

    if (is_resize || state_flipped || !main_win) {
        printf("\033[2J\033[H");
        recreate_ui_components(max_y, max_x, current_art_source, current_title_sig, art_w, art_h);
        last_player_active = player_active;
    }

    if (main_win) werase(main_win);

    if (!player_active) {
        draw_fallback_screen(main_win, max_y - 2, max_x - 2);
    } else {
        if (is_resize && meta_sub) {
            delwin(meta_sub); meta_sub = NULL;
        }
        if (is_resize && lyr_sub) {
            delwin(lyr_sub);  lyr_sub = NULL;
        }

        if (!meta_sub || !lyr_sub) {
            int meta_h = 11;
            int split_x = art_w + 6;

            if (horizontal) {
                // Meta sits under the Art on the left
                meta_sub = derwin(main_win, safe_h - art_h - 2, split_x - 4, art_h + 2, 2);
                // Lyrics takes the entire right side
                lyr_sub = derwin(main_win, safe_h, safe_w - split_x - 1, 0, split_x + 1);
            } else {
                // Vertical: Meta under Art, Lyrics under Meta
                meta_sub = derwin(main_win, meta_h, safe_w - 4, art_h + 2, 2);
                int lyr_y = art_h + meta_h + 3;
                int lyr_h = safe_h - lyr_y;
                if (lyr_h > 0) {
                    lyr_sub = derwin(main_win, lyr_h, safe_w - 4, lyr_y, 2);
                }
            }
        }

        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub) render_lyrics_pane(lyr_sub);

        update_panels();
        doupdate();

        if (is_new_track || is_resize) {
            update_and_render_art(current_art_source, current_title_sig, art_w, art_h, false);
        }
    }

    update_panels();
    doupdate();
}
