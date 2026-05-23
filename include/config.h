#ifndef CONFIG_H
#define CONFIG_H

#include <chafa.h>

#define ART_MODE_COUNT 24

typedef struct {
    int id;
    const char *name;
    ChafaSymbolTags tags;
    ChafaPixelMode pixel_mode;
    int is_ascii;
} ArtModeEntry;

extern const ArtModeEntry art_modes[ART_MODE_COUNT];

void set_art_debounce(int ms);
int get_art_debounce(void);

#endif
