#ifndef METADATA_H
#define METADATA_H

#define MAX_LYRIC_LINES 5000

typedef struct {
    char title[512], artist[512], album[512], player[64], url[4096], status[32];
    char genre[256], composer[512], file_path[4096];
    char *lyrics[MAX_LYRIC_LINES];
    int lyric_count;
} Metadata;

extern Metadata m;

void clear_metadata();
void parse_block(char *block);

#endif
