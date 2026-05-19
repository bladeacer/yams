#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "config.h"

static void test_art_debounce_default(void **state) {
    (void)state;
    assert_int_equal(get_art_debounce(), 200);
}

static void test_art_debounce_set_get(void **state) {
    (void)state;
    set_art_debounce(500);
    assert_int_equal(get_art_debounce(), 500);
    set_art_debounce(100);
    assert_int_equal(get_art_debounce(), 100);
}

static void test_art_debounce_clamp_low(void **state) {
    (void)state;
    set_art_debounce(-50);
    assert_int_equal(get_art_debounce(), 0);
}

static void test_art_debounce_clamp_high(void **state) {
    (void)state;
    set_art_debounce(10000);
    assert_int_equal(get_art_debounce(), 5000);
}

static void test_art_debounce_edge_zero(void **state) {
    (void)state;
    set_art_debounce(0);
    assert_int_equal(get_art_debounce(), 0);
}

static void test_art_debounce_edge_max(void **state) {
    (void)state;
    set_art_debounce(5000);
    assert_int_equal(get_art_debounce(), 5000);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_art_debounce_default),
        cmocka_unit_test(test_art_debounce_set_get),
        cmocka_unit_test(test_art_debounce_clamp_low),
        cmocka_unit_test(test_art_debounce_clamp_high),
        cmocka_unit_test(test_art_debounce_edge_zero),
        cmocka_unit_test(test_art_debounce_edge_max),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
