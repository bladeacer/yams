#ifndef ART_MANAGER_H
#define ART_MANAGER_H

#include <stdbool.h>

extern bool art_refresh_requested;

void free_art(void);
void update_and_render_art(const char* source, const char* title, int w, int h, bool force);
void replay_art_buffer(void);
void reset_art(void);
void expire_art_debounce(void);
long get_last_render_time(void);
long get_last_metadata_change_time(void);
bool is_art_active(void);
bool is_retry_pending(void);

#endif
