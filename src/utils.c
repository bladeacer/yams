#include <time.h>
#include <string.h>
#include <ctype.h>
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
        if (*src == '\'') { 
            // Close quote, add escaped quote, reopen quote
            if (dest - escaped < 8186) {
                memcpy(dest, "'\\''", 4); 
                dest += 4; 
            }
        } else { 
            *dest++ = *src; 
        }
        src++;
    }
    *dest = '\0';
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
