#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdio.h>
#include <cmocka.h>
#include "metadata.h"

static void test_clear_metadata(void **state) {
    (void)state;
    m.title[0] = 'x';
    m.artist[0] = 'y';
    m.lyric_count = 5;
    clear_metadata();
    assert_int_equal(m.lyric_count, 0);
    assert_int_equal(strlen(m.title), 0);
    assert_int_equal(strlen(m.artist), 0);
    assert_int_equal(strlen(m.album), 0);
    assert_int_equal(strlen(m.status), 0);
}

static void test_parse_block_null(void **state) {
    (void)state;
    clear_metadata();
    assert_false(parse_block(NULL));
}

static void test_parse_block_empty(void **state) {
    (void)state;
    clear_metadata();
    assert_false(parse_block(""));
}

static void test_parse_block_title_artist(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Bohemian Rhapsody	ARTIST|Queen	STATUS|Playing";
    assert_true(parse_block(input));
    assert_string_equal(m.title, "Bohemian Rhapsody");
    assert_string_equal(m.artist, "Queen");
    assert_string_equal(m.status, "Playing");
}

static void test_parse_block_album_genre_composer(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ALBUM|A Night at the Opera	GENRE|Rock	COMPOSER|Freddie Mercury";
    assert_false(parse_block(input));
    assert_string_equal(m.album, "A Night at the Opera");
    assert_string_equal(m.genre, "Rock");
    assert_string_equal(m.composer, "Freddie Mercury");
}

static void test_parse_block_length(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Test	LENGTH|354000000";
    assert_true(parse_block(input));
    assert_string_equal(m.length_str, "5:54");
}

static void test_parse_block_length_zero(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Test	LENGTH|0";
    assert_true(parse_block(input));
    assert_string_equal(m.length_str, "0:00");
}

static void test_parse_block_player_path(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "PLAYER|spotify	PATH|/home/user/music/song.flac";
    assert_false(parse_block(input));
    assert_string_equal(m.player, "spotify");
    assert_string_equal(m.file_path, "/home/user/music/song.flac");
}

static void test_parse_block_art_url(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ART|https://i.scdn.co/image/abc123";
    assert_false(parse_block(input));
    assert_string_equal(m.art_url, "https://i.scdn.co/image/abc123");
}

static void test_parse_block_art_url_skip_data(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ART|data:image/png;base64,abc";
    assert_false(parse_block(input));
    assert_int_equal(strlen(m.art_url), 0);
}

static void test_parse_block_lyrics_on_new_song(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|New Song	ARTIST|Me	LYRICS|line1\nline2\nline3\n";
    assert_true(parse_block(input));
    assert_int_equal(m.lyric_count, 3);
    assert_string_equal(m.lyrics[0], "line1");
    assert_string_equal(m.lyrics[1], "line2");
    assert_string_equal(m.lyrics[2], "line3");
}

static void test_parse_block_status_change_only(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Playing";
    assert_true(parse_block(input));
    assert_string_equal(m.status, "Playing");
}

static void test_parse_block_no_change(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Same Song";
    assert_true(parse_block(input));
    assert_string_equal(m.title, "Same Song");
    assert_false(parse_block(input));
}

static void test_parse_block_hash_in_value(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Song #1 (feat. Artist)	ARTIST|Rock Band	ALBUM|Greatest # Hits";
    assert_true(parse_block(input));
    assert_string_equal(m.title, "Song #1 (feat. Artist)");
    assert_string_equal(m.artist, "Rock Band");
    assert_string_equal(m.album, "Greatest # Hits");
}

static void test_parse_block_order_independence(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ARTIST|Band	TITLE|Song	ALBUM|Album	GENRE|Jazz	STATUS|Paused";
    assert_true(parse_block(input));
    assert_string_equal(m.artist, "Band");
    assert_string_equal(m.title, "Song");
    assert_string_equal(m.album, "Album");
    assert_string_equal(m.genre, "Jazz");
    assert_string_equal(m.status, "Paused");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_clear_metadata),
        cmocka_unit_test(test_parse_block_null),
        cmocka_unit_test(test_parse_block_empty),
        cmocka_unit_test(test_parse_block_title_artist),
        cmocka_unit_test(test_parse_block_album_genre_composer),
        cmocka_unit_test(test_parse_block_length),
        cmocka_unit_test(test_parse_block_length_zero),
        cmocka_unit_test(test_parse_block_player_path),
        cmocka_unit_test(test_parse_block_art_url),
        cmocka_unit_test(test_parse_block_art_url_skip_data),
        cmocka_unit_test(test_parse_block_lyrics_on_new_song),
        cmocka_unit_test(test_parse_block_status_change_only),
        cmocka_unit_test(test_parse_block_no_change),
        cmocka_unit_test(test_parse_block_hash_in_value),
        cmocka_unit_test(test_parse_block_order_independence),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
