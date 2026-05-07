#ifndef UI_H
#define UI_H

#include <stdbool.h>

// TODO: Move externs to getters/setters

extern bool resize_pending;
extern int scroll_pos;

void reset_art();
void init_ui();
void cleanup_ui(int sig);
void draw_dashboard();
void trigger_resize();

#endif
