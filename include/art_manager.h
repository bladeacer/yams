#ifndef ART_MANAGER_H
#define ART_MANAGER_H

#include <sys/types.h>

extern bool art_refresh_requested;

void update_and_render_art(const char* source, const char* title, int w, int h, bool force);
void replay_art_buffer(void);
long get_last_render_time(void);
bool is_art_active(void);

#endif
