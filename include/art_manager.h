#ifndef ART_MANAGER_H
#define ART_MANAGER_H

#include <sys/types.h>

void update_and_render_art(const char* source, const char* title, int w, int h, bool force);
void kill_art_process(void);

#endif
