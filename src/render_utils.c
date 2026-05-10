#include "render_utils.h"
#include "utils.h"
#include "debug.h"
#include "metadata.h"
#include <string.h>
#include <stdlib.h>

const char* safe_str(const char* s) {
    return (s && strlen(s) > 0) ? s : "Unknown";
}

void format_display_url(char *dest, const char *src, size_t size) {
    if (!src || !*src) {
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

void draw_item(WINDOW *win, const char* key, const char* desc, 
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

void draw_help_text(WINDOW *win, int y, int x) {
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

void draw_metadata_line(WINDOW *win, int *y, const char *label, const char *value, int pair, bool bold) {
    if (!value || !win || *y >= getmaxy(win)) return;

    if (pair > 0) wattron(win, COLOR_PAIR(pair));
    if (bold) wattron(win, A_BOLD);

    mvwaddstr(win, *y, 0, label);
    int val_x = strlen(label);
    int max_w = getmaxx(win) - val_x - 1;

    mvwaddnstr(win, *y, val_x, value, max_w);
    (*y)++;

    if (bold) wattroff(win, A_BOLD);
    if (pair > 0) wattroff(win, COLOR_PAIR(pair));
}

void draw_fallback_screen(WINDOW *win, int max_y, int max_x) {
    int mid_y = max_y / 2;
    int mid_x = max_x / 2;
    werase(win);

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
    wnoutrefresh(win);
}
