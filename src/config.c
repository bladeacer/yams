#include "config.h"

static int art_debounce_ms = 200;

const ArtModeEntry art_modes[ART_MODE_COUNT] = {
    { 0,  "ascii",     0,                                        1 },
    { 1,  "braille",   CHAFA_SYMBOL_TAG_BRAILLE,                 0 },
    { 2,  "block",     CHAFA_SYMBOL_TAG_BLOCK,                   0 },
    { 3,  "dot",       CHAFA_SYMBOL_TAG_DOT,                     0 },
    { 4,  "border",    CHAFA_SYMBOL_TAG_BORDER,                  0 },
    { 5,  "diagonal",  CHAFA_SYMBOL_TAG_DIAGONAL,                0 },
    { 6,  "half",      CHAFA_SYMBOL_TAG_HALF,                    0 },
    { 7,  "quad",      CHAFA_SYMBOL_TAG_QUAD,                    0 },
    { 8,  "sextant",   CHAFA_SYMBOL_TAG_SEXTANT,                 0 },
    { 9,  "technical", CHAFA_SYMBOL_TAG_TECHNICAL,               0 },
    { 10, "geometric", CHAFA_SYMBOL_TAG_GEOMETRIC,               0 },
    { 11, "latin",     CHAFA_SYMBOL_TAG_LATIN,                   0 },
    { 12, "digit",     CHAFA_SYMBOL_TAG_DIGIT,                   0 },
    { 13, "alpha",     CHAFA_SYMBOL_TAG_ALPHA,                   0 },
    { 14, "stipple",   CHAFA_SYMBOL_TAG_STIPPLE,                 0 },
    { 15, "solid",     CHAFA_SYMBOL_TAG_SOLID,                   0 },
    { 16, "inverted",  CHAFA_SYMBOL_TAG_INVERTED,                0 },
    { 17, "narrow",    CHAFA_SYMBOL_TAG_NARROW,                  0 },
    { 18, "wide",      CHAFA_SYMBOL_TAG_WIDE,                    0 },
    { 19, "ambiguous", CHAFA_SYMBOL_TAG_AMBIGUOUS,               0 },
    { 20, "legacy",    CHAFA_SYMBOL_TAG_LEGACY,                  0 },
    { 21, "extra",     CHAFA_SYMBOL_TAG_EXTRA,                   0 },
    { 22, "all",       (ChafaSymbolTags)(CHAFA_SYMBOL_TAG_ALL
                        & ~CHAFA_SYMBOL_TAG_EXTRA
                        & ~CHAFA_SYMBOL_TAG_BAD),                 0 },
};

void set_art_debounce(int ms) {
    if (ms < 0) ms = 0;
    if (ms > 5000) ms = 5000;
    art_debounce_ms = ms;
}

int get_art_debounce(void) {
    return art_debounce_ms;
}
