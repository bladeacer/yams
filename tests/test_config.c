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

static void test_player_name_default_empty(void **state) {
    (void)state;
    assert_string_equal(get_player_name(), "");
}

static void test_player_name_set_get(void **state) {
    (void)state;
    set_player_name("spotify");
    assert_string_equal(get_player_name(), "spotify");
    set_player_name("mpd");
    assert_string_equal(get_player_name(), "mpd");
}

static void test_player_name_reset_to_auto(void **state) {
    (void)state;
    set_player_name("spotify");
    set_player_name("");
    assert_string_equal(get_player_name(), "");
}

static void test_player_name_null_resets(void **state) {
    (void)state;
    set_player_name("spotify");
    set_player_name(NULL);
    assert_string_equal(get_player_name(), "");
}

static void test_player_switch_auto_to_specific(void **state) {
    (void)state;
    set_player_name("");
    assert_string_equal(get_player_name(), "");
    set_player_name("firefox");
    assert_string_equal(get_player_name(), "firefox");
}

static void test_player_switch_specific_to_auto(void **state) {
    (void)state;
    set_player_name("tauon");
    assert_string_equal(get_player_name(), "tauon");
    set_player_name("");
    assert_string_equal(get_player_name(), "");
}

static void test_player_switch_between_specific(void **state) {
    (void)state;
    set_player_name("spotify");
    assert_string_equal(get_player_name(), "spotify");
    set_player_name("firefox");
    assert_string_equal(get_player_name(), "firefox");
    set_player_name("tauon");
    assert_string_equal(get_player_name(), "tauon");
}

static void test_player_name_buffer_truncated(void **state) {
    (void)state;
    char long_name[128];
    memset(long_name, 'A', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    set_player_name(long_name);
    const char *result = get_player_name();
    assert_int_equal(strlen(result), 63);
    set_player_name("");
    assert_string_equal(get_player_name(), "");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_art_debounce_default),
        cmocka_unit_test(test_art_debounce_set_get),
        cmocka_unit_test(test_art_debounce_clamp_low),
        cmocka_unit_test(test_art_debounce_clamp_high),
        cmocka_unit_test(test_art_debounce_edge_zero),
        cmocka_unit_test(test_art_debounce_edge_max),
        cmocka_unit_test(test_player_name_default_empty),
        cmocka_unit_test(test_player_name_set_get),
        cmocka_unit_test(test_player_name_reset_to_auto),
        cmocka_unit_test(test_player_name_null_resets),
        cmocka_unit_test(test_player_switch_auto_to_specific),
        cmocka_unit_test(test_player_switch_specific_to_auto),
        cmocka_unit_test(test_player_switch_between_specific),
        cmocka_unit_test(test_player_name_buffer_truncated),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
