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

char* sh_escape(const char* src) {
    static char escaped[1024];
    char *d = escaped;
    char *limit = escaped + sizeof(escaped) - 5;

    while (*src && d < limit) {
        if (*src == '\'') {
            *d++ = '\''; *d++ = '\\'; *d++ = '\''; *d++ = '\'';
        } else {
            *d++ = *src;
        }
        src++;
    }
    *d = '\0';
    return escaped;
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

float get_image_aspect(const char* path) {
    // If no path, return square aspect
    if (!path || strlen(path) < 2) return 1.0f;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 \"%s\"", path);
    
    FILE* fp = popen(cmd, "r");
    if (!fp) return 1.0f;
    
    int w = 0, h = 0;
    if (fscanf(fp, "%dx%d", &w, &h) != 2 || h == 0) {
        pclose(fp);
        return 1.0f;
    }
    pclose(fp);
    return (float)w / (float)h;
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

static int preferred_renderer = 0;

int get_preferred_renderer(void) {
    return preferred_renderer;
}

void set_preferred_renderer(int type) {
    preferred_renderer = type;
}
