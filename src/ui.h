#ifndef UI_H
#define UI_H

#include <stdbool.h>

extern bool resize_pending;
extern int scroll_pos;

void init_ui();
void cleanup_ui(int sig);
void draw_dashboard();
void trigger_resize();

#endif
