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
    assert_true(parse_block(input));
    assert_string_equal(m.art_url, "https://i.scdn.co/image/abc123");

    /* Repeating the same art must not keep requesting redraws. */
    char again[] = "ART|https://i.scdn.co/image/abc123";
    assert_false(parse_block(again));
}

static void test_parse_block_art_url_skip_data(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ART|data:image/png;base64,abc";
    assert_false(parse_block(input));
    assert_int_equal(strlen(m.art_url), 0);
}

static void test_parse_block_art_url_at_sentinel(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "PATH|/path/to/song.flac\tART|@";
    assert_true(parse_block(input));
    assert_string_equal(m.art_url, "/path/to/song.flac");
}

static void test_parse_block_art_url_at_sentinel_remote_path(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "PATH|https://example.test/watch?v=abc\tART|@";
    assert_false(parse_block(input));
    assert_int_equal(strlen(m.art_url), 0);
}

static void test_parse_block_art_url_at_sentinel_remote_path_clears_art(void **state) {
    (void)state;
    clear_metadata();
    char first[] = "ART|https://i.scdn.co/image/abc123";
    assert_true(parse_block(first));

    char input[] = "PATH|https://example.test/watch?v=abc\tART|@";
    assert_true(parse_block(input));
    assert_int_equal(strlen(m.art_url), 0);
}

static void test_parse_block_art_url_at_sentinel_no_path(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "ART|@";
    assert_false(parse_block(input));
    assert_int_equal(strlen(m.art_url), 0);
}

static void test_parse_block_art_url_data_with_path(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "PATH|/path/to/song.flac\tART|data:image/png;base64,abc";
    assert_true(parse_block(input));
    assert_string_equal(m.art_url, "/path/to/song.flac");
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

static void test_parse_block_lrc_timestamps(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Synced\tLYRICS|[00:01.00]one\n[00:05.50]two\n"
                   "[ar:Some Artist]\nplain line\n";
    assert_true(parse_block(input));
    assert_int_equal(m.lyric_count, 3);
    assert_string_equal(m.lyrics[0], "one");
    assert_string_equal(m.lyrics[1], "two");
    assert_string_equal(m.lyrics[2], "plain line");
    assert_int_equal(m.lyric_times[0], 1000000);
    assert_int_equal(m.lyric_times[1], 5500000);
    assert_int_equal(m.lyric_times[2], -1);

    assert_int_equal(metadata_lyric_index_at(500000), -1);
    assert_int_equal(metadata_lyric_index_at(1000000), 0);
    assert_int_equal(metadata_lyric_index_at(2000000), 0);
    assert_int_equal(metadata_lyric_index_at(6000000), 1);
}

static void test_parse_block_lrc_multiple_tags(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "TITLE|Multi\tLYRICS|[00:02.00][00:08.50]chorus\n";
    assert_true(parse_block(input));
    assert_int_equal(m.lyric_count, 2);
    assert_string_equal(m.lyrics[0], "chorus");
    assert_string_equal(m.lyrics[1], "chorus");
    assert_int_equal(m.lyric_times[0], 2000000);
    assert_int_equal(m.lyric_times[1], 8500000);
}

static void test_lyrics_refresh_without_title_change(void **state) {
    (void)state;
    clear_metadata();

    char first[] = "TITLE|Same Song\tARTIST|Same Artist\tLYRICS|first line\n";
    assert_true(parse_block(first));
    assert_int_equal(m.lyric_count, 1);
    assert_string_equal(m.lyrics[0], "first line");

    /* The metadata stream repeats the title on every update. New lyrics must
     * still replace the old ones even when the song identity is unchanged. */
    char second[] = "TITLE|Same Song\tARTIST|Same Artist\tLYRICS|second line\n";
    assert_true(parse_block(second));
    assert_int_equal(m.lyric_count, 1);
    assert_string_equal(m.lyrics[0], "second line");
}

static void test_lyrics_forced_refresh(void **state) {
    (void)state;
    clear_metadata();

    char first[] = "TITLE|Song\tARTIST|Artist\tLYRICS|only line\n";
    char again[] = "TITLE|Song\tARTIST|Artist\tLYRICS|only line\n";
    assert_true(parse_block(first));
    assert_false(parse_block(again));

    metadata_request_refresh();
    char forced[] = "TITLE|Song\tARTIST|Artist\tLYRICS|only line\n";
    assert_true(parse_block(forced));
    assert_int_equal(m.lyric_count, 1);
}

static void test_parse_block_position_not_path(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "POSITION|2500000\tPATH|/music/song.flac";
    parse_block(input);
    assert_int_equal(m.position_us, 2500000);
    assert_string_equal(m.file_path, "/music/song.flac");
    assert_true(metadata_position_us() >= 2500000);
}

static void test_apply_seek_advances_position(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Playing\tPOSITION|5000000\tLENGTH|60000000";
    parse_block(input);

    long long before = metadata_position_us();
    metadata_apply_seek(5000000);
    long long after = metadata_position_us();

    /* Allow for the milliseconds that elapse during the calls. */
    assert_true(after >= before + 4990000);
}

static void test_apply_seek_clamps_bounds(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Paused\tPOSITION|5000000\tLENGTH|10000000";
    parse_block(input);

    metadata_apply_seek(-20000000);
    assert_int_equal(metadata_position_us(), 0);

    metadata_apply_seek(20000000);
    assert_int_equal(metadata_position_us(), 10000000);
}

static void test_apply_seek_ignores_stale_position_sample(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Playing\tPOSITION|5000000\tLENGTH|60000000";
    parse_block(input);

    metadata_apply_seek(5000000);
    assert_int_equal(m.position_us, 10000000);

    /* A block emitted before the seek must not pull the UI backwards. */
    char stale[] = "POSITION|5000000";
    parse_block(stale);
    assert_int_equal(m.position_us, 10000000);

    /* A confirming sample ends the guard and is accepted. */
    char fresh[] = "POSITION|10000000";
    parse_block(fresh);
    assert_int_equal(m.position_us, 10000000);
}

static void test_optimistic_status_toggle(void **state) {
    (void)state;
    clear_metadata();

    char playing[] = "STATUS|Playing";
    assert_true(parse_block(playing));
    assert_string_equal(m.status, "Playing");

    metadata_optimistic_toggle_status();
    assert_string_equal(m.status, "Paused");

    /* A stale update from the player must not undo the instant local flip. */
    char stale[] = "STATUS|Playing";
    assert_false(parse_block(stale));
    assert_string_equal(m.status, "Paused");

    /* The confirmation is accepted. */
    char confirmed[] = "STATUS|Paused";
    assert_false(parse_block(confirmed));
    assert_string_equal(m.status, "Paused");
}

static void test_format_timestamp(void **state) {
    (void)state;
    char buf[32];
    format_timestamp(0, buf, sizeof(buf));
    assert_string_equal(buf, "0:00");
    format_timestamp(65000000, buf, sizeof(buf));
    assert_string_equal(buf, "1:05");
    format_timestamp(3725000000LL, buf, sizeof(buf));
    assert_string_equal(buf, "1:02:05");
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

static void test_player_active_false_when_empty(void **state) {
    (void)state;
    clear_metadata();
    bool active = (m.status[0] != '\0' && m.status[0] != 'S');
    assert_false(active);
}

static void test_player_active_false_when_stopped(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Stopped";
    parse_block(input);
    bool active = (m.status[0] != '\0' && m.status[0] != 'S');
    assert_false(active);
}

static void test_player_active_true_when_playing(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Playing";
    parse_block(input);
    bool active = (m.status[0] != '\0' && m.status[0] != 'S');
    assert_true(active);
}

static void test_player_active_true_when_paused(void **state) {
    (void)state;
    clear_metadata();
    char input[] = "STATUS|Paused";
    parse_block(input);
    bool active = (m.status[0] != '\0' && m.status[0] != 'S');
    assert_true(active);
}

static void test_full_ui_fallback_transition_cycle(void **state) {
    (void)state;
    clear_metadata();
    assert_string_equal(m.status, "");
    assert_false(m.status[0] != '\0' && m.status[0] != 'S');

    char play[] = "TITLE|Song\tARTIST|Artist\tSTATUS|Playing";
    parse_block(play);
    assert_string_equal(m.status, "Playing");
    assert_true(m.status[0] != '\0' && m.status[0] != 'S');

    char stop[] = "STATUS|Stopped";
    parse_block(stop);
    assert_string_equal(m.status, "Stopped");
    assert_false(m.status[0] != '\0' && m.status[0] != 'S');

    clear_metadata();
    assert_string_equal(m.status, "");
    assert_false(m.status[0] != '\0' && m.status[0] != 'S');
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
        cmocka_unit_test(test_parse_block_art_url_at_sentinel),
        cmocka_unit_test(test_parse_block_art_url_at_sentinel_remote_path),
        cmocka_unit_test(test_parse_block_art_url_at_sentinel_remote_path_clears_art),
        cmocka_unit_test(test_parse_block_art_url_at_sentinel_no_path),
        cmocka_unit_test(test_parse_block_art_url_data_with_path),
        cmocka_unit_test(test_parse_block_lyrics_on_new_song),
        cmocka_unit_test(test_parse_block_status_change_only),
        cmocka_unit_test(test_parse_block_no_change),
        cmocka_unit_test(test_parse_block_hash_in_value),
        cmocka_unit_test(test_parse_block_order_independence),
        cmocka_unit_test(test_rapid_media_forward),
        cmocka_unit_test(test_rapid_status_toggle),
        cmocka_unit_test(test_rapid_song_switch_with_overlap),
        cmocka_unit_test(test_rapid_alternating_clear),
        cmocka_unit_test(test_parse_block_lrc_timestamps),
        cmocka_unit_test(test_parse_block_lrc_multiple_tags),
        cmocka_unit_test(test_lyrics_refresh_without_title_change),
        cmocka_unit_test(test_lyrics_forced_refresh),
        cmocka_unit_test(test_parse_block_position_not_path),
        cmocka_unit_test(test_apply_seek_advances_position),
        cmocka_unit_test(test_apply_seek_clamps_bounds),
        cmocka_unit_test(test_apply_seek_ignores_stale_position_sample),
        cmocka_unit_test(test_optimistic_status_toggle),
        cmocka_unit_test(test_format_timestamp),
        cmocka_unit_test(test_safe_str_null),
        cmocka_unit_test(test_safe_str_empty),
        cmocka_unit_test(test_safe_str_value),
        cmocka_unit_test(test_snapshot_copy),
        cmocka_unit_test(test_snapshot_empty_fields),
        cmocka_unit_test(test_snapshot_full_length),
        cmocka_unit_test(test_player_active_false_when_empty),
        cmocka_unit_test(test_player_active_false_when_stopped),
        cmocka_unit_test(test_player_active_true_when_playing),
        cmocka_unit_test(test_player_active_true_when_paused),
        cmocka_unit_test(test_full_ui_fallback_transition_cycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
