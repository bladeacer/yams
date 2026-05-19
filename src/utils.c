#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "utils.h"

long get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src;
            src++;
        }
    }
    *dst = '\0';
}

static int seek_interval = 5;

int get_seek_interval(void) {
    return seek_interval;
}

void set_seek_interval(int seconds) {
    if (seconds < 1) seconds = 1;
    if (seconds > 60) seconds = 60;
    seek_interval = seconds;
}

static int braille_mode = 0;

int get_braille_mode(void) {
    return braille_mode;
}

void set_braille_mode(int mode) {
    braille_mode = mode ? 1 : 0;
}
