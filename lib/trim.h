#ifndef TRIM_H
#define TRIM_H

#include <stdint.h>

typedef struct { int top, bottom, left, right; } BBox;

BBox find_content_bbox(const uint8_t *pixels, int w, int h, int fuzz);
uint8_t *crop_pixels(const uint8_t *src, int src_w, int src_h,
                     BBox bbox, int *out_w, int *out_h);

#endif
