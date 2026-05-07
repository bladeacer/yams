#ifndef METADATA_H
#define METADATA_H

#define MAX_LYRIC_LINES 1024

typedef struct {
    char title[512];
    char artist[512];
    char album[512];
    char status[64];
    char art_url[4096];
    char file_path[4096];
    char genre[512];
    char composer[1024];
    char player[64];
    char length_str[32];
    
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
    char lyric_buffer[65536];
} Metadata;

extern Metadata m;

void clear_metadata();
bool parse_block(char *block);

#endif
