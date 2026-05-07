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

void reset_art() {
    last_art_path[0] = '\0';
    last_title_sig[0] = '\0';
}

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
    DBG("Processing string: %s", s);
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

    char seek_str[32];

    snprintf(seek_str, sizeof(seek_str), "Seek: %ds", get_seek_interval());
    draw_item(win, "-/+", seek_str, &cur_y, &cur_x, x, max_h, max_w);

    const char *r_name = (get_preferred_renderer() == 1) ? "jp2a" : "chafa";
    snprintf(seek_str, sizeof(seek_str), "Art: %s", r_name);
    draw_item(win, "a", seek_str, &cur_y, &cur_x, x, max_h, max_w);

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
        mvwprintw(win, mid_y - 6 + i, mid_x - 18, "%s", logo[i]);
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

    draw_help_text(win, mid_y + 6, mid_x - 28);
}

static void draw_metadata_line(WINDOW *win, int *y, const char *label, const char *value, int color_pair, bool bold) {
    if (!value || *y >= getmaxy(win)) return;

    if (color_pair > 0) wattron(win, COLOR_PAIR(color_pair));
    if (bold) wattron(win, A_BOLD);

    mvwaddstr(win, *y, 0, label);
    
    int val_x = strlen(label);
    int max_w = getmaxx(win) - val_x - 1;

    mvwaddnstr(win, *y, val_x, value, max_w);
    (*y)++;

    if (bold) wattroff(win, A_BOLD);
    if (color_pair > 0) wattroff(win, COLOR_PAIR(color_pair));
}

static void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    werase(win);

    int cur_y = 0;
    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s", safe_str(m.player), safe_str(m.status));

    draw_metadata_line(win, &cur_y, "Player:   ", player_info, 1, true);
    draw_metadata_line(win, &cur_y, "Duration: ", m.length_str, 1, true);
    draw_metadata_line(win, &cur_y, "Title:    ", safe_str(m.title), 1, true);

    draw_metadata_line(win, &cur_y, "Artist:   ", safe_str(m.artist), 2, false);
    draw_metadata_line(win, &cur_y, "Album:    ", safe_str(m.album), 2, false);
    draw_metadata_line(win, &cur_y, "Genre:    ", safe_str(m.genre), 2, false);
    draw_metadata_line(win, &cur_y, "Composer: ", safe_str(m.composer), 2, false);

    char disp_url[1024];
    format_display_url(disp_url, m.file_path, sizeof(disp_url));
    draw_metadata_line(win, &cur_y, "Source:   ", disp_url, 0, false);

    cur_y++; 
    draw_help_text(win, cur_y, 0);

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
        mvwprintw(win, 0, 0, "  (No lyrics found)");
        wattroff(win, A_DIM);
    }
    wnoutrefresh(win); 
}

static void update_and_render_art(
        const char* current_art_source, const char* current_title_sig, 
        int art_w, int art_h, bool force
        ) {
    
    if (!current_art_source || strlen(current_art_source) < 2) return;

    bool source_changed = strcmp(current_art_source, last_art_path) != 0;
    bool title_changed = strcmp(current_title_sig, last_title_sig) != 0;

    if (force || source_changed || title_changed) {
        render_art(3, 1, art_w, art_h);

        strncpy(last_art_path, current_art_source, sizeof(last_art_path) - 1);
        strncpy(last_title_sig, current_title_sig, sizeof(last_title_sig) - 1);
    }
}

static void recreate_ui_components(int max_y, int max_x) {
    
    if (main_panel) { del_panel(main_panel); main_panel = NULL; }
    if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
    if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
    if (main_win)   { delwin(main_win);   main_win = NULL; }

    resizeterm(max_y, max_x);
    
    clearok(curscr, TRUE);
    erase(); 
    
    main_win = newwin(max_y - 2, max_x - 2, 1, 1);
    main_panel = new_panel(main_win);

    wnoutrefresh(stdscr);

    reset_art();

    last_w = max_x; 
    last_h = max_y;
    resize_pending = false;
}


void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static bool first_run = true;
    static Metadata last_m;

    char *current_art_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status[0] != '\0' && strcmp(m.status, "Stopped") != 0);

    char current_title_sig[2048];
    snprintf(current_title_sig, sizeof(current_title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));

    bool metadata_changed = memcmp(&m, &last_m, sizeof(Metadata)) != 0;
    bool size_changed = (max_x != last_w || max_y != last_h);
    bool force_redraw = (size_changed || resize_pending || first_run);

    if (!metadata_changed && !force_redraw) return;

    if (max_x != last_w || max_y != last_h || first_run) {
        recreate_ui_components(max_y, max_x);
    }

    first_run = false;
    last_m = m;
    last_w = max_x;
    last_h = max_y;

    if (!main_win) {
        if (resize_pending || force_redraw) {
            clear();
        }
        recreate_ui_components(max_y, max_x);
    }

    bool horizontal = (max_x > 90);
    int safe_w = max_x - 2;
    int safe_h = max_y - 2;

    int art_w, art_h;
    if (horizontal) {
        art_w = (int)(safe_w * 0.4);
        if (art_w > 54) art_w = 54;
        art_h = (int)(safe_h * 0.6);
    } else {
        art_w = safe_w - 4;
        if (art_w > 50) art_w = 50;
        art_h = (int)(safe_h * 0.4); 
    }

    if (!player_active) {
        draw_fallback_screen(main_win, max_y - 2, max_x - 2);
    } else {
        if (force_redraw) {
            if (meta_sub) { delwin(meta_sub); meta_sub = NULL; }
            if (lyr_sub)  { delwin(lyr_sub);  lyr_sub = NULL; }
        }

        if (!meta_sub || !lyr_sub) {
            if (horizontal) {
                int split_x = art_w + 4;
                meta_sub = derwin(main_win, safe_h - art_h - 2, split_x - 2, art_h + 1, 1);
                lyr_sub = derwin(main_win, safe_h - 1, safe_w - split_x - 1, 0, split_x + 1);
            } else {
                int meta_h = 11; 
                meta_sub = derwin(main_win, meta_h, safe_w - 2, art_h + 1, 1);
                int lyr_y = art_h + meta_h + 2;
                int lyr_h = safe_h - lyr_y;
                if (lyr_h > 0) {
                    lyr_sub = derwin(main_win, lyr_h, safe_w - 2, lyr_y, 1);
                }
            }
        }

        if (meta_sub) render_metadata_pane(meta_sub);
        if (lyr_sub) render_lyrics_pane(lyr_sub);
    }

    update_panels();
    doupdate();

    if (player_active) {
        bool force = force_redraw || metadata_changed;
        update_and_render_art(current_art_source, current_title_sig, art_w, art_h, force);
    }

    resize_pending = false;
}
