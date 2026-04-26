#ifndef UTILS_H
#define UTILS_H

long get_ms();
char* sh_escape(const char* src);
void url_decode(char *dst, const char *src);
float get_image_aspect(const char* path);

#endif
