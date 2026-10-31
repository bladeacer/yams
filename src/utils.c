#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"
#include "config.h"

long get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int xasprintf(char **strp, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        *strp = NULL;
        return -1;
    }
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        va_end(ap2);
        *strp = NULL;
        return -1;
    }
    vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    *strp = buf;
    return n;
}

bool is_remote_url(const char *url) {
    if (!url) return false;
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
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

static int art_mode = 0;

int get_art_mode(void) {
    return art_mode;
}

void set_art_mode(int mode) {
    if (mode < 0) mode = 0;
    if (mode >= ART_MODE_COUNT) mode = ART_MODE_COUNT - 1;
    art_mode = mode;
}
