#ifndef UTILS_H
#define UTILS_H

long get_ms();
void url_decode(char *dst, const char *src);
int get_seek_interval(void);
void set_seek_interval(int seconds);
int get_braille_mode(void);
void set_braille_mode(int mode);

#endif
