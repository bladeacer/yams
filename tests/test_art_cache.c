#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <cmocka.h>
#include "art.h"
#include "art_manager.h"
#include "metadata.h"
#include "utils.h"

static char *make_temp_dir(void) {
    char *t = strdup("/tmp/yams_cache_test_XXXXXX");
    if (!t) return NULL;
    if (!mkdtemp(t)) { free(t); return NULL; }
    return t;
}

static void mkdir_p(char *dir) {
    char *p = dir;
    while ((p = strchr(p + 1, '/'))) {
        *p = '\0';
        mkdir(dir, 0755);
        *p = '/';
    }
    mkdir(dir, 0755);
}

static int setup(void **state) {
    char *dir = make_temp_dir();
    if (!dir) return -1;
    setenv("XDG_CACHE_HOME", dir, 1);
    *state = dir;
    art_cache_bump();
    return 0;
}

static int teardown(void **state) {
    char *dir = (char *)*state;
    if (!dir) return 0;
    char *cmd;
    asprintf(&cmd, "rm -rf '%s'", dir);
    system(cmd);
    free(cmd);
    free(dir);
    return 0;
}

static void test_cache_empty_on_init(void **state) {
    (void)state;
    char *info = get_cache_info();
    assert_non_null(info);
    assert_string_equal(info, "Art cache: empty");
    free(info);
}

static void test_cache_clear_empty_returns_msg(void **state) {
    (void)state;
    char *msg = clear_art_cache();
    assert_non_null(msg);
    assert_string_equal(msg, "Cache directory not found");
    free(msg);
}

static void test_cache_reports_correct_size(void **state) {
    char *dir = (char *)*state;
    char *cache_dir;
    asprintf(&cache_dir, "%s/yams/art", dir);
    mkdir_p(cache_dir);

    char *path;
    asprintf(&path, "%s/test_file.bin", cache_dir);
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    uint8_t data[4096] = {42};
    size_t written = fwrite(data, 1, sizeof(data), f);
    assert_int_equal(written, sizeof(data));
    fclose(f);
    free(path);

    art_cache_bump();

    char *info = get_cache_info();
    assert_non_null(info);
    assert_string_equal(info, "Art cache: 4.0 KB (1 file)");
    free(info);
    free(cache_dir);
}

static void test_cache_clear_removes_files(void **state) {
    char *dir = (char *)*state;
    char *cache_dir;
    asprintf(&cache_dir, "%s/yams/art", dir);
    mkdir_p(cache_dir);

    char *path;
    asprintf(&path, "%s/a.bin", cache_dir);
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    free(path);

    uint8_t data[1024] = {1};
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    asprintf(&path, "%s/b.bin", cache_dir);
    f = fopen(path, "wb");
    assert_non_null(f);
    free(path);

    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    art_cache_bump();

    char *info = get_cache_info();
    assert_non_null(info);
    assert_string_equal(info, "Art cache: 2.0 KB (2 files)");
    free(info);

    char *msg = clear_art_cache();
    assert_non_null(msg);
    assert_string_equal(msg, "Cleared 2.0 KB (2 files)");
    free(msg);

    info = get_cache_info();
    assert_non_null(info);
    assert_string_equal(info, "Art cache: 0 B (0 files)");
    free(info);

    free(cache_dir);
}

static void test_cache_version_returns_cached(void **state) {
    char *dir = (char *)*state;
    char *cache_dir;
    asprintf(&cache_dir, "%s/yams/art", dir);
    mkdir_p(cache_dir);

    char *info1 = get_cache_info();
    assert_non_null(info1);
    assert_string_equal(info1, "Art cache: 0 B (0 files)");
    free(info1);

    char *path;
    asprintf(&path, "%s/x.bin", cache_dir);
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    free(path);

    uint8_t data[512] = {0};
    fwrite(data, 1, sizeof(data), f);
    fclose(f);

    art_cache_bump();

    char *info2 = get_cache_info();
    assert_non_null(info2);
    assert_string_equal(info2, "Art cache: 512 B (1 file)");
    free(info2);

    free(cache_dir);
}

static void test_expire_debounce_sets_time_to_zero(void **state) {
    (void)state;
    reset_art();
    assert_true(get_last_metadata_change_time() > 0);
    expire_art_debounce();
    assert_int_equal(get_last_metadata_change_time(), 0);
}

static void test_reset_art_sets_current_time(void **state) {
    (void)state;
    expire_art_debounce();
    assert_int_equal(get_last_metadata_change_time(), 0);
    reset_art();
    long t = get_last_metadata_change_time();
    assert_true(t > 0);
    assert_true(t <= get_ms());
}

static void test_debounce_starts_zero_on_expire(void **state) {
    (void)state;
    expire_art_debounce();
    assert_int_equal(get_last_metadata_change_time(), 0);
    long now = get_ms();
    assert_true(now - get_last_metadata_change_time() >= 0);
}

static void test_art_source_prefers_remote_art_url(void **state) {
    (void)state;
    memset(&m, 0, sizeof(m));
    strcpy(m.art_url, "https://example.test/cover.jpg");
    strcpy(m.file_path, "/music/song.flac");
    assert_string_equal(art_source_for_current_track(), m.art_url);
}

static void test_art_source_uses_local_file_path(void **state) {
    (void)state;
    memset(&m, 0, sizeof(m));
    strcpy(m.file_path, "/music/song.flac");
    assert_string_equal(art_source_for_current_track(), m.file_path);
}

static void test_art_source_ignores_remote_track_page(void **state) {
    (void)state;
    memset(&m, 0, sizeof(m));
    strcpy(m.file_path, "https://music.youtube.com/watch?v=abc");
    assert_null(art_source_for_current_track());
}

static void test_art_source_ignores_remote_path_copied_to_art(void **state) {
    (void)state;
    memset(&m, 0, sizeof(m));
    strcpy(m.file_path, "https://music.youtube.com/watch?v=abc");
    strcpy(m.art_url, m.file_path);
    assert_null(art_source_for_current_track());
}

static void test_art_source_ignores_data_uri(void **state) {
    (void)state;
    memset(&m, 0, sizeof(m));
    strcpy(m.art_url, "data:image/png;base64,AAAA");
    strcpy(m.file_path, "/music/song.flac");
    assert_string_equal(art_source_for_current_track(), m.file_path);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_cache_empty_on_init, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cache_clear_empty_returns_msg, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cache_reports_correct_size, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cache_clear_removes_files, setup, teardown),
        cmocka_unit_test_setup_teardown(test_cache_version_returns_cached, setup, teardown),
        cmocka_unit_test(test_expire_debounce_sets_time_to_zero),
        cmocka_unit_test(test_reset_art_sets_current_time),
        cmocka_unit_test(test_debounce_starts_zero_on_expire),
        cmocka_unit_test(test_art_source_prefers_remote_art_url),
        cmocka_unit_test(test_art_source_uses_local_file_path),
        cmocka_unit_test(test_art_source_ignores_remote_track_page),
        cmocka_unit_test(test_art_source_ignores_remote_path_copied_to_art),
        cmocka_unit_test(test_art_source_ignores_data_uri),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
