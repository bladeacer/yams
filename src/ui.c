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

void init_ui() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0); 
    
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, 250, -1);
    }
    DBG("Terminal initialized: %dx%d", COLS, LINES);
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

static void recreate_ui_components(int max_y, int max_x) {
    DBG("Reconstructing UI components: %dx%d", max_x, max_y);
    
    printf("\033[2J\033[H\033[0m"); 
    fflush(stdout);

    if (main_panel) { del_panel(main_panel); main_panel = NULL; }
    if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
    if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
    if (main_win)   { delwin(main_win);   main_win = NULL; }

    main_win = newwin(max_y - 2, max_x - 2, 1, 1);
    main_panel = new_panel(main_win);

    erase();
    clearok(stdscr, TRUE);
    refresh();
}

static void render_metadata_pane(WINDOW *win) {
    if (!win) return;
    werase(win);

    wattron(win, COLOR_PAIR(1) | A_BOLD);
    mvwprintw(win, 0, 0, "  Player:   %s | %s", safe_str(m.player), safe_str(m.status));
    mvwprintw(win, 1, 0, "  Duration: %s ", m.length_str);
    wattroff(win, A_BOLD);
    
    mvwprintw(win, 2, 0, "  Title:    %s", safe_str(m.title));
    wattron(win, COLOR_PAIR(2));
    mvwprintw(win, 3, 0, "  Artist:   %s", safe_str(m.artist));
    mvwprintw(win, 4, 0, "  Album:    %s", safe_str(m.album));
    wattroff(win, COLOR_PAIR(2));
    
    mvwprintw(win, 5, 0, "  Genre:    %s", safe_str(m.genre));
    mvwprintw(win, 6, 0, "  Composer: %s", safe_str(m.composer));
    
    wattron(win, A_DIM);
    char disp_url[1024];
    format_display_url(disp_url, m.file_path, sizeof(disp_url));
    mvwprintw(win, 7, 0, "  Source:   %s", disp_url);
    wattroff(win, A_DIM);
    
    draw_help_text(win, 10, 0);

    wnoutrefresh(win);
}

static void render_lyrics_pane(WINDOW *win) {
    werase(win);
    int h, w;
    getmaxyx(win, h, w);

    if (m.lyric_count > 0) {
        wattron(win, COLOR_PAIR(3));
        for (int i = 0; i < h && i < m.lyric_count; i++) {
            mvwaddnstr(win, i, 0, m.lyrics[i], w);
        }
        wattroff(win, COLOR_PAIR(3));
    } else {
        wattron(win, A_DIM);
        mvwprintw(win, 2, 0, "  (No lyrics found)");
        wattroff(win, A_DIM);
    }
}

void draw_dashboard() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    static char last_art_path[4096] = {0};
    static char last_title_sig[2048] = {0};
    static int last_w = 0, last_h = 0;
    static bool last_player_active = false;

    char *current_art_source = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    bool player_active = (m.status[0] != '\0' && strcmp(m.status, "Stopped") != 0);
    
    char current_title_sig[2048];
    snprintf(current_title_sig, sizeof(current_title_sig), "%s%s", safe_str(m.title), safe_str(m.artist));

    bool is_resize = (max_x != last_w || max_y != last_h || resize_pending);
    bool is_new_track = (strcmp(current_art_source, last_art_path) != 0) || 
                        (strcmp(current_title_sig, last_title_sig) != 0);
    bool state_flipped = (player_active != last_player_active);

    if (is_resize || state_flipped || !main_win) {
        recreate_ui_components(max_y, max_x);
        
        last_art_path[0] = '\0';
        last_title_sig[0] = '\0';

        extern bool force_art_redraw; 
        force_art_redraw = true;
        
        last_w = max_x; 
        last_h = max_y;
        last_player_active = player_active;
        resize_pending = false;
    }

    erase(); 
    if (main_win) werase(main_win);

    if (!player_active) {
        const char* msg = "Awaiting Player Input...";
        mvwprintw(main_win, (max_y - 2) / 2, (max_x - 2 - strlen(msg)) / 2, "%s", msg);
        draw_help_text(main_win, (max_y - 2) / 2 + 2, (max_x - 2 - 26) / 2);
    } else {
        int safe_w = max_x - 2;
        int safe_h = max_y - 2;
        bool horizontal = (max_x > 90);

        int art_w = horizontal ? (int)(safe_w * 0.4) : safe_w - 6;
        int max_dim = horizontal ? 54 : 50;
        if (art_w > max_dim) art_w = max_dim;
        int art_h = art_w * 0.5;

        if (!meta_sub || !lyr_sub) {
            if (horizontal) {
                int split = art_w + 6;
                meta_sub = derwin(main_win, 11, split - 4, art_h + 2, 2);
                lyr_sub = derwin(main_win, safe_h, safe_w - split - 2, 0, split + 2);
            } else {
                meta_sub = derwin(main_win, 11, safe_w - 4, art_h + 2, 2);
                lyr_sub = derwin(main_win, safe_h - art_h - 15, safe_w - 4, art_h + 14, 2);
            }
        }

        render_metadata_pane(meta_sub);
        render_lyrics_pane(lyr_sub);

        update_panels();
        doupdate();

        if (is_new_track || is_resize) {
            DBG("Redrawing Art: %s", current_art_source);
            
            render_art(3, 1, art_w, art_h);
            
            strncpy(last_art_path, current_art_source, sizeof(last_art_path) - 1);
            strncpy(last_title_sig, current_title_sig, sizeof(last_title_sig) - 1);
        }
    }

    update_panels();
    doupdate();
}
