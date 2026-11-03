#include "layout.h"

void compute_layout(int max_y, int max_x, Geometry *out) {
    out->max_x = max_x;
    out->max_y = max_y;
    out->horizontal = (max_x > 90);

    int safe_w = max_x - 2;
    int safe_h = max_y - 2;

    /* Keep the geometry inside the screen even on extremely small terminals. */
    if (safe_w < 1) safe_w = 1;
    if (safe_h < 1) safe_h = 1;

    if (out->horizontal) {
        out->art_w = (safe_w * 0.4 > 54) ? 54 : (int)(safe_w * 0.4);
        if (out->art_w < 10) out->art_w = 10;
        out->art_h = (int)(safe_h * 0.6);
        if (out->art_h < 4) out->art_h = 4;
    } else {
        out->art_w = (safe_w - 4 > 50) ? 50 : safe_w - 4;
        if (out->art_w < 10) out->art_w = 10;
        out->art_h = (int)(safe_h * 0.4);
        if (out->art_h < 4) out->art_h = 4;
    }

    /* Minimums must never push the art area outside a small terminal. */
    if (out->art_w > safe_w) out->art_w = safe_w;
    if (out->art_h > safe_h) out->art_h = safe_h;
}
