#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "layout.h"

static void test_80x24(void **state) {
    (void)state;
    Geometry g;
    compute_layout(24, 80, &g);
    assert_int_equal(g.max_y, 24);
    assert_int_equal(g.max_x, 80);
    assert_false(g.horizontal);
    assert_int_equal(g.art_w, 50);
    assert_int_equal(g.art_h, 8);
}

static void test_40x20(void **state) {
    (void)state;
    Geometry g;
    compute_layout(20, 40, &g);
    assert_false(g.horizontal);
    assert_int_equal(g.art_w, 34);
    assert_int_equal(g.art_h, 7);
}

static void test_120x40(void **state) {
    (void)state;
    Geometry g;
    compute_layout(40, 120, &g);
    assert_true(g.horizontal);
    assert_int_equal(g.art_w, 47);
    assert_int_equal(g.art_h, 22);
}

static void test_1920x1080(void **state) {
    (void)state;
    Geometry g;
    compute_layout(54, 192, &g);
    assert_true(g.horizontal);
    assert_int_equal(g.art_w, 54);
    assert_int_equal(g.art_h, 31);
}

static void test_4k(void **state) {
    (void)state;
    Geometry g;
    compute_layout(108, 384, &g);
    assert_true(g.horizontal);
    assert_int_equal(g.art_w, 54);
    assert_int_equal(g.art_h, 63);
}

static void test_ultrawide(void **state) {
    (void)state;
    Geometry g;
    compute_layout(72, 344, &g);
    assert_true(g.horizontal);
    assert_int_equal(g.art_w, 54);
    assert_int_equal(g.art_h, 42);
}

static void test_2880x1880(void **state) {
    (void)state;
    Geometry g;
    compute_layout(94, 288, &g);
    assert_true(g.horizontal);
    assert_int_equal(g.art_w, 54);
    assert_int_equal(g.art_h, 55);
}

static void test_min_safe_area(void **state) {
    (void)state;
    Geometry g;
    compute_layout(6, 14, &g);
    assert_false(g.horizontal);
    assert_int_equal(g.art_w, 10);
    assert_int_equal(g.art_h, 4);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_80x24),
        cmocka_unit_test(test_40x20),
        cmocka_unit_test(test_120x40),
        cmocka_unit_test(test_1920x1080),
        cmocka_unit_test(test_4k),
        cmocka_unit_test(test_ultrawide),
        cmocka_unit_test(test_2880x1880),
        cmocka_unit_test(test_min_safe_area),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
