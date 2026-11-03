#ifndef SELECTOR_LAYOUT_H
#define SELECTOR_LAYOUT_H

#include <stdbool.h>

/* Geometry for the modal list selector. Keeping this math in its own module
 * makes the responsive behaviour testable without opening a real terminal. */
typedef struct {
    int h, w;
    int y, x;
    int list_y;
    int vis_rows;
    int footer_y;   /* first footer row, or -1 when the selector is compact */
    int filter_y;   /* filter input row, or -1 when not shown */
    int filter_x;
    int filter_w;
    bool compact;
} SelectorLayout;

/* Compute a selector that fits inside max_y x max_x. Returns false when the
 * terminal cannot show even a single item. max_item_w is the longest item
 * string in display cells. */
bool compute_selector_layout(int max_y, int max_x, int count, int max_item_w,
                             bool filterable, SelectorLayout *out);

#endif
