#include "layout.h"

void compute_layout(int max_y, int max_x, Geometry *out) {
    out->max_x = max_x;
    out->max_y = max_y;
    out->horizontal = (max_x > 90);

    int safe_w = max_x - 2;
    int safe_h = max_y - 2;

    if (safe_w < 4) safe_w = 4;
    if (safe_h < 4) safe_h = 4;

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
}
