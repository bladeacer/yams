#ifndef RENDER_UTILS_H
#define RENDER_UTILS_H

#include <ncurses.h>

/* Basic drawing primitives */
void draw_item(WINDOW *win, const char* key, const char* desc, 
               int *cy, int *cx, int x_orig, int max_h, int max_w);
               
void draw_metadata_line(WINDOW *win, int *y, const char *label, 
                        const char *value, int color_pair, bool bold);

/* Specialized screens */
void clear_art_area(void);
void draw_help_text(WINDOW *win, int y, int x);
void draw_fallback_screen(WINDOW *win, int max_y, int max_x);

/* String formatting */
const char* safe_str(const char* s);
void format_display_url(char *dest, const char *src, size_t size);

#endif
