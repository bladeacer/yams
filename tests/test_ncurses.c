#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <panel.h>
#include <cmocka.h>

#include "layout.h"
#include "selector_layout.h"

static void test_selector_layout_fits_every_size(void **state) {
    (void)state;

    static const struct {
        int rows, cols, count, item_w;
        bool filterable;
    } cases[] = {
        {4, 10, 25, 10, true},
        {5, 20, 3, 8, true},
        {6, 24, 25, 20, false},
        {8, 30, 25, 25, true},
        {10, 12, 2, 6, true},
        {11, 80, 25, 30, true},
        {24, 80, 25, 30, true},
        {40, 120, 5, 12, true},
        {60, 240, 25, 30, true},
        {13, 47, 25, 27, false},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        SelectorLayout lo;
        bool ok = compute_selector_layout(cases[i].rows, cases[i].cols,
                                          cases[i].count, cases[i].item_w,
                                          cases[i].filterable, &lo);
        assert_true(ok);
        assert_true(lo.h >= 4);
        assert_true(lo.h <= cases[i].rows);
        assert_true(lo.w <= cases[i].cols);
        assert_true(lo.y >= 0);
        assert_true(lo.x >= 0);
        assert_true(lo.list_y >= 1);
        assert_true(lo.vis_rows >= 1);
        assert_true(lo.vis_rows <= cases[i].count);
        if (lo.footer_y >= 0) {
            assert_true(lo.footer_y > lo.list_y);
            assert_true(lo.footer_y < lo.h);
        }
        if (lo.filter_y >= 0) {
            assert_true(lo.filter_y < lo.h);
            assert_true(lo.filter_x < lo.w);
        }
    }

    SelectorLayout lo;
    assert_false(compute_selector_layout(3, 80, 10, 10, true, &lo));
    assert_false(compute_selector_layout(24, 9, 10, 10, true, &lo));
}

/* Drive real ncurses windows at a matrix of terminal sizes. newterm() writes to
 * temporary files, so this runs deterministically without a pty or a terminal. */
static void test_ncurses_instances_at_many_sizes(void **state) {
    (void)state;

    FILE *input = tmpfile();
    FILE *output = tmpfile();
    assert_non_null(input);
    assert_non_null(output);

    SCREEN *screen = newterm("xterm-256color", output, input);
    assert_non_null(screen);
    set_term(screen);

    static const struct {
        int rows, cols;
    } sizes[] = {
        {4, 10},   {5, 20},   {6, 24},   {8, 30},  {13, 47},
        {20, 40},  {24, 80},  {40, 120}, {60, 240},
    };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int rows = sizes[i].rows;
        int cols = sizes[i].cols;

        resizeterm(rows, cols);
        erase();
        refresh();

        WINDOW *full = newwin(rows, cols, 0, 0);
        assert_non_null(full);
        assert_int_equal(getmaxy(full), rows);
        assert_int_equal(getmaxx(full), cols);
        box(full, 0, 0);
        mvwprintw(full, 0, 1, " %dx%d ", rows, cols);
        wnoutrefresh(full);
        doupdate();

        Geometry g;
        compute_layout(rows, cols, &g);
        int safe_w = cols - 2;
        int safe_h = rows - 2;
        if (safe_w < 1) safe_w = 1;
        if (safe_h < 1) safe_h = 1;

        assert_true(g.art_w > 0);
        assert_true(g.art_h > 0);
        assert_true(g.art_w <= safe_w);
        assert_true(g.art_h <= safe_h);

        WINDOW *main_win = newwin(safe_h, safe_w, 1, 1);
        assert_non_null(main_win);
        PANEL *panel = new_panel(main_win);
        assert_non_null(panel);
        update_panels();
        doupdate();

        del_panel(panel);
        delwin(main_win);
        delwin(full);
    }

    endwin();
    delscreen(screen);
    fclose(output);
    fclose(input);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_selector_layout_fits_every_size),
        cmocka_unit_test(test_ncurses_instances_at_many_sizes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
