#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cmocka.h>

#include "stream.h"

static void test_strip_complete_data_art(void **state) {
    (void)state;
    char buf[] = "PLAYER|mpv\tART|data:image/png;base64,AAAA\tLYRICS|hi\t[[END]]";
    size_t len = strlen(buf);
    bool skip = false;

    stream_strip_data_art(buf, &len, &skip);
    buf[len] = '\0';

    assert_false(skip);
    assert_string_equal(buf, "PLAYER|mpv\tART|@\tLYRICS|hi\t[[END]]");
}

static void test_strip_data_art_split_across_chunks(void **state) {
    (void)state;
    char first[] = "PLAYER|mpv\tART|data:image/png;base64,AAAA";
    size_t first_len = strlen(first);
    bool skip = false;

    stream_strip_data_art(first, &first_len, &skip);
    first[first_len] = '\0';
    assert_true(skip);
    assert_string_equal(first, "PLAYER|mpv\tART|@");

    char second[] = "BBBBBB\tLYRICS|hi\t[[END]]";
    size_t second_len = strlen(second);
    stream_strip_data_art(second, &second_len, &skip);
    second[second_len] = '\0';
    assert_false(skip);
    assert_string_equal(second, "\tLYRICS|hi\t[[END]]");

    char joined[128];
    snprintf(joined, sizeof(joined), "%s%s", first, second);
    assert_string_equal(joined,
                        "PLAYER|mpv\tART|@\tLYRICS|hi\t[[END]]");
}

static void test_partial_art_marker_detection(void **state) {
    (void)state;

    const char *partial = "TITLE|x\tART|da";
    assert_int_equal(stream_partial_art_marker(partial, strlen(partial)),
                     strlen("\tART|da"));

    const char *complete = "TITLE|x\tART|data:image";
    assert_int_equal(stream_partial_art_marker(complete, strlen(complete)), 0);

    const char *plain = "TITLE|done";
    assert_int_equal(stream_partial_art_marker(plain, strlen(plain)), 0);

    const char *tab_only = "TITLE|x\t";
    assert_int_equal(stream_partial_art_marker(tab_only, strlen(tab_only)), 1);
}

static void test_split_marker_then_data_uri(void **state) {
    (void)state;

    /* Simulate the caller carrying the incomplete marker into the next read. */
    const char *first = "TITLE|x\tART|data:im";
    size_t first_len = strlen(first);
    size_t hold = stream_partial_art_marker(first, first_len);
    assert_true(hold > 0);

    char work[128];
    memcpy(work, first + first_len - hold, hold);
    strcpy(work + hold, "age/png;base64,AAAA\tLYRICS|hi\t[[END]]");
    size_t len = strlen(work);
    bool skip = false;

    stream_strip_data_art(work, &len, &skip);
    work[len] = '\0';

    assert_false(skip);
    assert_string_equal(work, "\tART|@\tLYRICS|hi\t[[END]]");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_strip_complete_data_art),
        cmocka_unit_test(test_strip_data_art_split_across_chunks),
        cmocka_unit_test(test_partial_art_marker_detection),
        cmocka_unit_test(test_split_marker_then_data_uri),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
