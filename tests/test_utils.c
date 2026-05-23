#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>
#include "utils.h"
#include "config.h"

static void test_url_decode_no_encoding(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "hello world");
    assert_string_equal(dst, "hello world");
}

static void test_url_decode_percent_encoded(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "hello%20world");
    assert_string_equal(dst, "hello world");
}

static void test_url_decode_plus_space(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "hello+world");
    assert_string_equal(dst, "hello world");
}

static void test_url_decode_mixed(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "a%2Fb%20c+d");
    assert_string_equal(dst, "a/b c d");
}

static void test_url_decode_hex_uppercase(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "%2F%3A%23");
    assert_string_equal(dst, "/:#");
}

static void test_url_decode_hex_lowercase(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "%2f%3a%23");
    assert_string_equal(dst, "/:#");
}

static void test_url_decode_empty(void **state) {
    (void)state;
    char dst[256] = {0};
    url_decode(dst, "");
    assert_string_equal(dst, "");
}

static void test_url_decode_special_chars(void **state) {
    (void)state;
    char dst[256];
    url_decode(dst, "%25%26%3D");
    assert_string_equal(dst, "%&=");
}

static void test_seek_interval_default(void **state) {
    (void)state;
    assert_int_equal(get_seek_interval(), 5);
}

static void test_seek_interval_set_get(void **state) {
    (void)state;
    set_seek_interval(10);
    assert_int_equal(get_seek_interval(), 10);
    set_seek_interval(3);
    assert_int_equal(get_seek_interval(), 3);
}

static void test_seek_interval_clamp_low(void **state) {
    (void)state;
    set_seek_interval(0);
    assert_int_equal(get_seek_interval(), 1);
}

static void test_seek_interval_clamp_high(void **state) {
    (void)state;
    set_seek_interval(100);
    assert_int_equal(get_seek_interval(), 60);
}

static void test_art_mode_default(void **state) {
    (void)state;
    assert_int_equal(get_art_mode(), 0);
}

static void test_art_mode_set_get(void **state) {
    (void)state;
    set_art_mode(0);
    assert_int_equal(get_art_mode(), 0);
    set_art_mode(1);
    assert_int_equal(get_art_mode(), 1);
    set_art_mode(ART_MODE_COUNT);
    assert_int_equal(get_art_mode(), ART_MODE_COUNT - 1);
    set_art_mode(0);
    assert_int_equal(get_art_mode(), 0);
}

static void test_art_mode_table_coverage(void **state) {
    (void)state;
    assert_int_equal(ART_MODE_COUNT, 24);
    for (int i = 0; i < ART_MODE_COUNT; i++) {
        assert_non_null(art_modes[i].name);
        assert_int_not_equal(strlen(art_modes[i].name), 0);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_url_decode_no_encoding),
        cmocka_unit_test(test_url_decode_percent_encoded),
        cmocka_unit_test(test_url_decode_plus_space),
        cmocka_unit_test(test_url_decode_mixed),
        cmocka_unit_test(test_url_decode_hex_uppercase),
        cmocka_unit_test(test_url_decode_hex_lowercase),
        cmocka_unit_test(test_url_decode_empty),
        cmocka_unit_test(test_url_decode_special_chars),
        cmocka_unit_test(test_seek_interval_default),
        cmocka_unit_test(test_seek_interval_set_get),
        cmocka_unit_test(test_seek_interval_clamp_low),
        cmocka_unit_test(test_seek_interval_clamp_high),
        cmocka_unit_test(test_art_mode_default),
        cmocka_unit_test(test_art_mode_set_get),
        cmocka_unit_test(test_art_mode_table_coverage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
