#ifndef METADATA_H
#define METADATA_H

#define MAX_LYRIC_LINES 5000

typedef struct {
    char title[1024], artist[1024], album[1024], player[128], art_url[4096], status[64];
    char genre[512], composer[1024], file_path[4096];
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
} Metadata;

extern Metadata m;

void clear_metadata();
void parse_block(char *block);

#endif
