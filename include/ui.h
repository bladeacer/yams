#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <signal.h>

// The global that main.c uses for the WINCH signal
extern volatile sig_atomic_t resize_pending;

// Set by main.c's exit signal handler; modal loops poll it to unwind.
extern volatile sig_atomic_t exit_requested;

// Mappings: These are implemented in terminal.c
void init_ui();
void cleanup_ui(void);

// These are implemented in art_manager.c
void reset_art();
long get_last_metadata_change_time();

// These remain in ui.c
void draw_dashboard();
void show_art_mode_selector();
void show_player_selector(void);
void show_status_msg(const char *msg);

#endif
