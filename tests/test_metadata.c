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

static void test_rapid_media_forward(void **state) {
    (void)state;
    for (int i = 0; i < 100; i++) {
        clear_metadata();
        char buf[512];
        if (i % 2 == 0) {
            strcpy(buf, "TITLE|Song A\tARTIST|Artist A\tSTATUS|Playing\tLENGTH|300000000");
        } else {
            strcpy(buf, "TITLE|Song B\tARTIST|Artist B\tSTATUS|Playing\tLENGTH|200000000");
        }
        assert_true(parse_block(buf));

        if (i % 2 == 0) {
            assert_string_equal(m.title, "Song A");
            assert_string_equal(m.artist, "Artist A");
        } else {
            assert_string_equal(m.title, "Song B");
            assert_string_equal(m.artist, "Artist B");
        }
        assert_string_equal(m.status, "Playing");
    }
}

static void test_rapid_status_toggle(void **state) {
    (void)state;
    clear_metadata();

    for (int i = 0; i < 100; i++) {
        char buf[64];
        strcpy(buf, (i % 2 == 0) ? "STATUS|Playing" : "STATUS|Paused");
        parse_block(buf);

        if (i % 2 == 0) {
            assert_string_equal(m.status, "Playing");
        } else {
            assert_string_equal(m.status, "Paused");
        }
    }
}

static void test_rapid_song_switch_with_overlap(void **state) {
    (void)state;

    for (int round = 0; round < 5; round++) {
        clear_metadata();
        char buf_a[512], buf_b[512];
        strcpy(buf_a, "TITLE|Song A\tARTIST|Artist A\tSTATUS|Playing\tLENGTH|300000");
        strcpy(buf_b, "TITLE|Song B\tARTIST|Artist B\tSTATUS|Playing\tALBUM|Album B\tLENGTH|200000");

        assert_true(parse_block(buf_a));
        assert_string_equal(m.title, "Song A");
        assert_string_equal(m.artist, "Artist A");

        assert_true(parse_block(buf_b));
        assert_string_equal(m.title, "Song B");
        assert_string_equal(m.artist, "Artist B");
        assert_string_equal(m.album, "Album B");
        assert_string_equal(m.status, "Playing");
    }
}

static void test_rapid_alternating_clear(void **state) {
    (void)state;

    for (int i = 0; i < 50; i++) {
        clear_metadata();
        assert_string_equal(m.title, "");
        assert_string_equal(m.artist, "");
        assert_string_equal(m.status, "");

        char buf[128];
        strcpy(buf, "TITLE|Song\tARTIST|Artist\tSTATUS|Playing");
        assert_true(parse_block(buf));
        assert_string_equal(m.title, "Song");
        assert_string_equal(m.artist, "Artist");
        assert_string_equal(m.status, "Playing");
    }
}

static const char* safe_str_test(const char* s) {
    return (s && strlen(s) > 0) ? s : "Unknown";
}

static void test_safe_str_null(void **state) {
    (void)state;
    assert_string_equal(safe_str_test(NULL), "Unknown");
}

static void test_safe_str_empty(void **state) {
    (void)state;
    assert_string_equal(safe_str_test(""), "Unknown");
}

static void test_safe_str_value(void **state) {
    (void)state;
    assert_string_equal(safe_str_test("hello"), "hello");
    assert_string_equal(safe_str_test("Test Song"), "Test Song");
}

static void test_snapshot_copy(void **state) {
    (void)state;
    clear_metadata();

    char block[] = "TITLE|A long song title that might cause issues\t"
        "ARTIST|An artist name\tSTATUS|Playing\tALBUM|Test Album\t"
        "GENRE|Rock\tCOMPOSER|A Composer";
    assert_true(parse_block(block));

    char snap_title[512], snap_artist[512], snap_album[512];
    char snap_genre[256], snap_composer[256], snap_player[64], snap_status[64];
    strncpy(snap_title, m.title, sizeof(snap_title) - 1); snap_title[sizeof(snap_title) - 1] = '\0';
    strncpy(snap_artist, m.artist, sizeof(snap_artist) - 1); snap_artist[sizeof(snap_artist) - 1] = '\0';
    strncpy(snap_album, m.album, sizeof(snap_album) - 1); snap_album[sizeof(snap_album) - 1] = '\0';
    strncpy(snap_genre, m.genre, sizeof(snap_genre) - 1); snap_genre[sizeof(snap_genre) - 1] = '\0';
    strncpy(snap_composer, m.composer, sizeof(snap_composer) - 1); snap_composer[sizeof(snap_composer) - 1] = '\0';
    strncpy(snap_player, m.player, sizeof(snap_player) - 1); snap_player[sizeof(snap_player) - 1] = '\0';
    strncpy(snap_status, m.status, sizeof(snap_status) - 1); snap_status[sizeof(snap_status) - 1] = '\0';

    assert_string_equal(snap_title, "A long song title that might cause issues");
    assert_string_equal(snap_artist, "An artist name");
    assert_string_equal(snap_status, "Playing");
    assert_string_equal(snap_album, "Test Album");
    assert_string_equal(snap_genre, "Rock");
    assert_string_equal(snap_composer, "A Composer");

    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s",
             safe_str_test(snap_player), safe_str_test(snap_status));
    assert_string_equal(player_info, "Unknown - Playing");
}

static void test_snapshot_empty_fields(void **state) {
    (void)state;
    clear_metadata();

    char snap_status[64], snap_player[64], snap_title[512];
    strncpy(snap_status, m.status, sizeof(snap_status) - 1); snap_status[sizeof(snap_status) - 1] = '\0';
    strncpy(snap_player, m.player, sizeof(snap_player) - 1); snap_player[sizeof(snap_player) - 1] = '\0';
    strncpy(snap_title, m.title, sizeof(snap_title) - 1); snap_title[sizeof(snap_title) - 1] = '\0';

    assert_string_equal(snap_status, "");
    assert_string_equal(snap_player, "");

    char player_info[512];
    snprintf(player_info, sizeof(player_info), "%s - %s",
             safe_str_test(snap_player), safe_str_test(snap_status));
    assert_string_equal(player_info, "Unknown - Unknown");
    assert_string_equal(safe_str_test(snap_title), "Unknown");
}

static void test_snapshot_full_length(void **state) {
    (void)state;
    clear_metadata();

    // Fill title with max-length string
    char long_title[512];
    memset(long_title, 'A', sizeof(long_title) - 1);
    long_title[sizeof(long_title) - 1] = '\0';
    long_title[500] = '\0';

    char block[1024];
    snprintf(block, sizeof(block), "TITLE|%s\tARTIST|Artist\tSTATUS|Playing", long_title);
    assert_true(parse_block(block));

    char snap_title[512];
    strncpy(snap_title, m.title, sizeof(snap_title) - 1);
    snap_title[sizeof(snap_title) - 1] = '\0';

    assert_int_equal(strlen(snap_title), 500);
    assert_int_equal(snap_title[499], 'A');
    assert_int_equal(snap_title[500], '\0');
    assert_int_equal(snap_title[511], '\0');
    assert_string_equal(safe_str_test(snap_title), snap_title);

    // Test with string that exactly fills buffer minus 1
    clear_metadata();
    memset(long_title, 'B', sizeof(long_title) - 2);
    long_title[sizeof(long_title) - 2] = '\0';

    snprintf(block, sizeof(block), "TITLE|%s\tARTIST|Artist2\tSTATUS|Paused", long_title);
    assert_true(parse_block(block));

    strncpy(snap_title, m.title, sizeof(snap_title) - 1);
    snap_title[sizeof(snap_title) - 1] = '\0';

    assert_int_equal(strlen(snap_title), 510);
    assert_int_equal(snap_title[511], '\0');
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
        cmocka_unit_test(test_rapid_media_forward),
        cmocka_unit_test(test_rapid_status_toggle),
        cmocka_unit_test(test_rapid_song_switch_with_overlap),
        cmocka_unit_test(test_rapid_alternating_clear),
        cmocka_unit_test(test_safe_str_null),
        cmocka_unit_test(test_safe_str_empty),
        cmocka_unit_test(test_safe_str_value),
        cmocka_unit_test(test_snapshot_copy),
        cmocka_unit_test(test_snapshot_empty_fields),
        cmocka_unit_test(test_snapshot_full_length),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
