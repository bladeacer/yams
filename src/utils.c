#include <time.h>
#include <string.h>
#include "utils.h"

long get_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

char* sh_escape(const char* src) {
    static char escaped[8192];
    char *dest = escaped;
    while (*src && (dest - escaped < 8190)) {
        if (*src == '\'') { memcpy(dest, "'\\''", 4); dest += 4; }
        else { *dest++ = *src; }
        src++;
    }
    *dest = '\0';
    return escaped;
}
