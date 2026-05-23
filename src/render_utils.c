#include "render_utils.h"
#include "utils.h"
#include "config.h"
#include "debug.h"
#include "metadata.h"
#include "layout.h"
#include "art.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>

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
    draw_item(win, "r", "Retry Art", &cur_y, &cur_x, x, max_h, max_w);

    {
        static char clbl[64] = "";
        char *info = get_cache_info();
        if (info) {
            const char *v = strchr(info, ':');
            v = v ? v + 2 : info;
            snprintf(clbl, sizeof(clbl), "%s", v);
            free(info);
        }
        draw_item(win, "c", clbl[0] ? clbl : "Cache", &cur_y, &cur_x, x, max_h, max_w);
    }

    draw_item(win, "?", "Help", &cur_y, &cur_x, x, max_h, max_w);

    cur_y++;
    cur_x = x;

    char seek_str[32];
    snprintf(seek_str, sizeof(seek_str), "Seek: %ds", get_seek_interval());
    draw_item(win, "-/+", seek_str, &cur_y, &cur_x, x, max_h, max_w);

    snprintf(seek_str, sizeof(seek_str), "Art: %s", art_modes[get_art_mode()].name);
    draw_item(win, "a", seek_str, &cur_y, &cur_x, x, max_h, max_w);
    wattroff(win, A_BOLD);
}

void draw_metadata_line(WINDOW *win, int *y, const char *label, const char *value, int pair, bool bold) {
    if (!value || !win || *y >= getmaxy(win)) return;

    if (pair > 0) wattron(win, COLOR_PAIR(pair));
    if (bold) wattron(win, A_BOLD);

    mvwaddstr(win, *y, 0, label);
    int val_x = getcurx(win);
    int max_w = getmaxx(win) - val_x;
    if (max_w < 0) max_w = 0;

    wchar_t wbuf[2048];
    size_t n = mbstowcs(wbuf, value, 2048);
    if (n != (size_t)-1 && n > 0) {
        int value_w = wcswidth(wbuf, n);
        if (value_w > max_w) {
            int i = 0, col = 0;
            while (i < (int)n && col + wcwidth(wbuf[i]) <= max_w) {
                col += wcwidth(wbuf[i]);
                i++;
            }
            wbuf[i] = L'\0';
            mvwaddwstr(win, *y, val_x, wbuf);
        } else {
            mvwaddwstr(win, *y, val_x, wbuf);
        }
    } else {
        mvwaddnstr(win, *y, val_x, value, max_w);
    }

    (*y)++;

    if (bold) wattroff(win, A_BOLD);
    if (pair > 0) wattroff(win, COLOR_PAIR(pair));
}

void clear_art_area(void) {
    if (geo.art_w <= 0 || geo.art_h <= 0) return;
    int art_x = 2, art_y = 1;
    int abs_y = 1 + art_y;
    int abs_x = 1 + art_x;
    char *clear = malloc((size_t)geo.art_w + 1);
    if (!clear) return;
    memset(clear, ' ', geo.art_w);
    clear[geo.art_w] = '\0';
    char buf[1024];
    for (int r = 0; r < geo.art_h; r++) {
        int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH%s", abs_y + r, abs_x, clear);
        if (n > 0) write(STDOUT_FILENO, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf)));
    }
    free(clear);
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
