#include "layout.h"
#include <ncurses.h>
#include <panel.h>
#include "ui.h"
#include "art_manager.h"

Geometry geo; 

WINDOW *main_win = NULL;
PANEL  *main_panel = NULL;
WINDOW *lyr_sub = NULL;
WINDOW *meta_sub = NULL;

void recreate_layout() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Clean up old instances
    if (main_panel) { del_panel(main_panel); main_panel = NULL; }
    if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
    if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
    if (main_win)   { delwin(main_win);   main_win = NULL; }

    // Physical screen management
    resizeterm(max_y, max_x);
    clearok(curscr, TRUE);
    erase();
    wnoutrefresh(stdscr);

    compute_layout(max_y, max_x, &geo);

    int safe_w = max_x - 2;
    int safe_h = max_y - 2;
    if (safe_w < 4) safe_w = 4;
    if (safe_h < 4) safe_h = 4;

    main_win = newwin(safe_h, safe_w, 1, 1);
    leaveok(main_win, TRUE);
    main_panel = new_panel(main_win);

    if (geo.horizontal) {
        int split_x = geo.art_w + 4;
        if (split_x >= safe_w) split_x = safe_w - 1;

        int meta_h = safe_h - geo.art_h - 2;
        if (meta_h < 1) meta_h = 1;
        int meta_w = split_x - 2;
        if (meta_w < 4) meta_w = 4;
        meta_sub = derwin(main_win, meta_h, meta_w, geo.art_h + 1, 1);

        int lyr_w = safe_w - split_x - 1;
        if (lyr_w < 4) { lyr_w = 4; split_x = safe_w - lyr_w - 1; }
        lyr_sub = derwin(main_win, safe_h - 1, lyr_w, 0, split_x + 1);
    } else {
        int meta_h = 11;
        if (geo.art_h + meta_h + 2 > safe_h)
            meta_h = safe_h - geo.art_h - 2;
        if (meta_h < 1) meta_h = 1;

        meta_sub = derwin(main_win, meta_h, safe_w - 2, geo.art_h + 1, 1);

        int lyr_y = geo.art_h + meta_h + 2;
        int lyr_h = safe_h - lyr_y;
        if (lyr_h > 3) {
            lyr_sub = derwin(main_win, lyr_h, safe_w - 2, lyr_y, 1);
        }
    }

    reset_art();
    expire_art_debounce();
    resize_pending = false;
}
