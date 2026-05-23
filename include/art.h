#ifndef ART_H
#define ART_H

#include <chafa.h>

char *render_art_to_buffer(int x, int y, int w, int h,
                            ChafaSymbolTags tags, ChafaPixelMode pixel_mode, int is_ascii);

#endif
