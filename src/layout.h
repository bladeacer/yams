#ifndef LAYOUT_H
#define LAYOUT_H
#include <ncurses.h>
#include <panel.h>

typedef struct {
    int max_y, max_x;
    int art_w, art_h;
    bool horizontal;
} Geometry;

extern Geometry geo;
extern WINDOW *main_win;
extern WINDOW *meta_sub;
extern WINDOW *lyr_sub;
extern PANEL  *main_panel;

void recreate_layout();
#endif
