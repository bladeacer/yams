#include <stdbool.h>
#include <stddef.h>

#ifndef METADATA_H
#define METADATA_H

#define MAX_LYRIC_LINES 1024

typedef struct {
    int lyric_count;
    char status[64];
    char player[64];
    char length_str[32];
    long long length_us;
    long long position_us;
    long long position_received_ms;

    char *lyrics[MAX_LYRIC_LINES];
    /* Timestamp for each lyric line in microseconds, or -1 for plain text
     * lyrics that carry no timing information. */
    long long lyric_times[MAX_LYRIC_LINES];

    char title[512];
    char artist[512];
    char album[512];
    char genre[256];
    char composer[256];

    char art_url[2048];
    char file_path[2048];
} Metadata;

typedef struct {
    int lyric_count;
    char status[64];
    char player[64];
    char length_str[32];
    long long length_us;
    long long position_us;
    char position_str[32];
    char title[512];
    char artist[512];
    char album[512];
    char genre[256];
    char composer[256];
    char art_url[2048];
    char file_path[2048];
} MetadataSnap;

extern Metadata m;

void clear_metadata();
void snapshot_metadata(MetadataSnap *snap);
bool parse_block(char *block);

/* Force the next lyrics block to be reparsed even if its text is unchanged.
 * Used by the manual refresh key after the metadata stream is restarted. */
void metadata_request_refresh(void);

/* Milliseconds-since-epoch style helper for the current track position.
 * The value is interpolated from the last position sample while playing, so
 * synced lyrics keep moving between metadata updates. */
long long metadata_position_us(void);
int metadata_lyric_index(void);
int metadata_lyric_index_at(long long position_us);

/* Flip Playing/Paused locally for an immediate UI response. Incoming stale
 * status updates are ignored briefly until the player confirms the change. */
void metadata_optimistic_toggle_status(void);

/* Format microseconds as m:ss (or h:mm:ss for long tracks). */
void format_timestamp(long long us, char *buf, size_t size);

#endif
