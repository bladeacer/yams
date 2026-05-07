#ifndef UTILS_H
#define UTILS_H

long get_ms();
char* sh_escape(const char* src);
void url_decode(char *dst, const char *src);
float get_image_aspect(const char* path);
int get_seek_interval(void);
void set_seek_interval(int seconds);
int get_preferred_renderer(void);
void set_preferred_renderer(int type);

#endif
