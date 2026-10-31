#include <stdbool.h>

#ifndef METADATA_H
#define METADATA_H

#define MAX_LYRIC_LINES 1024

typedef struct {
    int lyric_count;
    char status[64];
    char player[64];
    char length_str[32];
    
    char *lyrics[MAX_LYRIC_LINES];
    
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

#endif
