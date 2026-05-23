#ifndef UI_H
#define UI_H

#include <stdbool.h>

// The global that main.c uses for the WINCH signal
extern bool resize_pending;

// Mappings: These are implemented in terminal.c
void init_ui();
void cleanup_ui(int sig);

// These are implemented in art_manager.c
void reset_art();
long get_last_metadata_change_time();

// These remain in ui.c
void draw_dashboard();
void show_art_mode_selector();

#endif
