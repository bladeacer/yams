#include "config.h"

static int art_debounce_ms = 200;

const ArtModeEntry art_modes[ART_MODE_COUNT] = {
    { 0,  "ascii",     0,                                        CHAFA_PIXEL_MODE_SYMBOLS, 1 },
    { 1,  "braille",   CHAFA_SYMBOL_TAG_BRAILLE,                 CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 2,  "block",     CHAFA_SYMBOL_TAG_BLOCK,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 3,  "dot",       CHAFA_SYMBOL_TAG_DOT,                     CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 4,  "border",    CHAFA_SYMBOL_TAG_BORDER,                  CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 5,  "diagonal",  CHAFA_SYMBOL_TAG_DIAGONAL,                CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 6,  "half",      CHAFA_SYMBOL_TAG_HALF,                    CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 7,  "quad",      CHAFA_SYMBOL_TAG_QUAD,                    CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 8,  "sextant",   CHAFA_SYMBOL_TAG_SEXTANT,                 CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 9,  "technical", CHAFA_SYMBOL_TAG_TECHNICAL,               CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 10, "geometric", CHAFA_SYMBOL_TAG_GEOMETRIC,               CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 11, "latin",     CHAFA_SYMBOL_TAG_LATIN,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 12, "digit",     CHAFA_SYMBOL_TAG_DIGIT,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 13, "alpha",     CHAFA_SYMBOL_TAG_ALPHA,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 14, "stipple",   CHAFA_SYMBOL_TAG_STIPPLE,                 CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 15, "solid",     CHAFA_SYMBOL_TAG_SOLID,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 16, "inverted",  CHAFA_SYMBOL_TAG_INVERTED,                CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 17, "narrow",    CHAFA_SYMBOL_TAG_NARROW,                  CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 18, "wide",      CHAFA_SYMBOL_TAG_WIDE,                    CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 19, "ambiguous", CHAFA_SYMBOL_TAG_AMBIGUOUS,               CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 20, "legacy",    CHAFA_SYMBOL_TAG_LEGACY,                  CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 21, "extra",     CHAFA_SYMBOL_TAG_EXTRA,                   CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 22, "all",       (ChafaSymbolTags)(CHAFA_SYMBOL_TAG_ALL
                        & ~CHAFA_SYMBOL_TAG_EXTRA
                        & ~CHAFA_SYMBOL_TAG_BAD),                CHAFA_PIXEL_MODE_SYMBOLS, 0 },
    { 23, "sixel",     0,                                        CHAFA_PIXEL_MODE_SIXELS,  0 },
    { 24, "kitty",     0,                                        CHAFA_PIXEL_MODE_KITTY,   0 },
};

void set_art_debounce(int ms) {
    if (ms < 0) ms = 0;
    if (ms > 5000) ms = 5000;
    art_debounce_ms = ms;
}

int get_art_debounce(void) {
    return art_debounce_ms;
}

static char selected_player[64] = {0};

void set_player_name(const char *name) {
    if (!name) { selected_player[0] = '\0'; return; }
    strncpy(selected_player, name, sizeof(selected_player) - 1);
    selected_player[sizeof(selected_player) - 1] = '\0';
}

const char *get_player_name(void) {
    return selected_player;
}
