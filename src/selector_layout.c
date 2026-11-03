#include "selector_layout.h"

#include <stddef.h>

bool compute_selector_layout(int max_y, int max_x, int count, int max_item_w,
                             bool filterable, SelectorLayout *out) {
    if (!out || count <= 0)
        return false;

    /* Below this there is no room for a border, one list row and sane input. */
    if (max_y < 4 || max_x < 10)
        return false;

    int desired_w = max_item_w + 6;
    if (desired_w < 30) desired_w = 30;
    if (desired_w > 64) desired_w = 64;

    int w = desired_w;
    if (w > max_x - 2) w = max_x - 2;
    if (w < 10) w = max_x;
    if (w > max_x) w = max_x;

    /* Footer takes three rows when filtering (help + help + filter) and two
     * otherwise. Drop it entirely on short terminals so the list keeps as many
     * rows as possible. */
    int footer = filterable ? 3 : 2;
    if (max_y < (filterable ? 11 : 9)) {
        footer = 0;
    }

    int desired_h = count + footer + 2;
    int h = desired_h;
    if (h > max_y) h = max_y;
    if (h < 4) h = 4;
    if (h > max_y) h = max_y;

    int list_y = 1; /* row 0 is the top border / title */
    int footer_y = footer > 0 ? h - footer - 1 : h - 1;
    if (footer_y <= list_y)
        footer_y = list_y + 1;
    if (footer_y > h - 1)
        footer_y = h - 1;

    int vis_rows = footer_y - list_y;
    if (vis_rows < 1)
        vis_rows = 1;
    if (vis_rows > count)
        vis_rows = count;

    out->h = h;
    out->w = w;
    out->y = (max_y - h) / 2;
    out->x = (max_x - w) / 2;
    out->list_y = list_y;
    out->vis_rows = vis_rows;
    out->footer_y = footer > 0 ? footer_y : -1;
    out->compact = (footer == 0);

    if (filterable && footer > 0) {
        out->filter_y = h - 2;
        out->filter_x = 10;
        out->filter_w = w - out->filter_x - 1;
        if (out->filter_w < 1) {
            out->filter_y = -1;
            out->filter_w = 0;
        }
    } else {
        out->filter_y = -1;
        out->filter_x = 0;
        out->filter_w = 0;
    }

    return true;
}
