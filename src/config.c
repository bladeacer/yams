#include "config.h"

static int art_debounce_ms = 200;

void set_art_debounce(int ms) {
    if (ms < 0) ms = 0;
    if (ms > 5000) ms = 5000;
    art_debounce_ms = ms;
}

int get_art_debounce(void) {
    return art_debounce_ms;
}
