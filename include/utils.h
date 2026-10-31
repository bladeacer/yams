#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>

long get_ms();
void url_decode(char *dst, const char *src);
bool is_remote_url(const char *url);

/* Allocate a formatted string. Returns the number of characters written
 * (>= 0) on success and -1 on failure, setting *strp to NULL. */
int xasprintf(char **strp, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
int get_seek_interval(void);
void set_seek_interval(int seconds);
int get_art_mode(void);
void set_art_mode(int mode);

#endif
