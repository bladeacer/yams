#include "trim.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int color_diff(const uint8_t *a, const uint8_t *b) {
    return abs((int)a[0] - b[0]) + abs((int)a[1] - b[1]) + abs((int)a[2] - b[2]);
}

BBox find_content_bbox(const uint8_t *pixels, int w, int h, int fuzz) {
    BBox bbox = {0, h - 1, 0, w - 1};
    if (w <= 0 || h <= 0) return bbox;

    uint8_t ref[3] = {pixels[0], pixels[1], pixels[2]};
    int threshold = fuzz * 3;

    for (bbox.top = 0; bbox.top < h; bbox.top++) {
        bool found = false;
        for (int x = 0; x < w; x++) {
            if (color_diff(&pixels[(bbox.top * w + x) * 4], ref) > threshold) {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    for (bbox.bottom = h - 1; bbox.bottom > bbox.top; bbox.bottom--) {
        bool found = false;
        for (int x = 0; x < w; x++) {
            if (color_diff(&pixels[(bbox.bottom * w + x) * 4], ref) > threshold) {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    for (bbox.left = 0; bbox.left < w; bbox.left++) {
        bool found = false;
        for (int y = bbox.top; y <= bbox.bottom; y++) {
            if (color_diff(&pixels[(y * w + bbox.left) * 4], ref) > threshold) {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    for (bbox.right = w - 1; bbox.right > bbox.left; bbox.right--) {
        bool found = false;
        for (int y = bbox.top; y <= bbox.bottom; y++) {
            if (color_diff(&pixels[(y * w + bbox.right) * 4], ref) > threshold) {
                found = true;
                break;
            }
        }
        if (found) break;
    }

    return bbox;
}

uint8_t *crop_pixels(const uint8_t *src, int src_w, int src_h,
                     BBox bbox, int *out_w, int *out_h) {
    (void)src_h;
    int new_w = bbox.right - bbox.left + 1;
    int new_h = bbox.bottom - bbox.top + 1;
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;

    uint8_t *dst = malloc((size_t)new_w * new_h * 4);
    if (!dst) return NULL;

    for (int y = 0; y < new_h; y++) {
        memcpy(dst + (size_t)y * new_w * 4,
               src + ((size_t)(bbox.top + y) * src_w + bbox.left) * 4,
               (size_t)new_w * 4);
    }

    *out_w = new_w;
    *out_h = new_h;
    return dst;
}
