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

/* Lightweight redraw for the interpolated position, the active lyric line and
 * the cache label. It never replays album art, so the one-second clock tick
 * cannot make the image flicker. */
void draw_clock_update(void);

/* Monotonic deadline at which the transient status message should be cleared,
 * or 0 when no status message is active. */
long get_status_expiry_at(void);

/* True when a track/art change is waiting for the debounce window. */
bool art_update_pending(void);

void show_art_mode_selector();
void show_player_selector(void);
void show_status_msg(const char *msg);

#endif
