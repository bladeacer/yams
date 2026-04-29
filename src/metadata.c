#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "metadata.h"
#include "debug.h"

Metadata m;

void clear_metadata() {
    for (int i = 0; i < m.lyric_count; i++) {
        if (m.lyrics[i]) {
            free(m.lyrics[i]);
            m.lyrics[i] = NULL;
        }
    }
    m.lyric_count = 0;
    memset(&m, 0, sizeof(Metadata));
}

void parse_block(char *block) {
    if (!block || strlen(block) < 10) return;

    clear_metadata();
    m.title[0] = m.artist[0] = m.album[0] = '\0';
    m.art_url[0] = '\0';

    char *current = block;
    char *next_sep;
    const char *delim = "¶";

    #define SET_METADATA(field, src, maxlen) { \
        strncpy(field, src, maxlen); \
        field[maxlen] = '\0'; \
    }
    
    while ((next_sep = strstr(current, delim)) != NULL) {
        *next_sep = '\0';
        char *token = current;
        
        size_t token_len = strlen(token);
        if (token_len == 0) {
            current = next_sep + 1;
            continue;
        }

        if (strncmp(token, "TITLE|", 6) == 0)      SET_METADATA(m.title, token + 6, 511)
        else if (strncmp(token, "ARTIST|", 7) == 0) SET_METADATA(m.artist, token + 7, 511)
        else if (strncmp(token, "ALBUM|", 6) == 0)  SET_METADATA(m.album, token + 6, 511)
        else if (strncmp(token, "STATUS|", 7) == 0) SET_METADATA(m.status, token + 7, 31)
        else if (strncmp(token, "PATH|", 5) == 0)   SET_METADATA(m.file_path, token + 5, 4095)
        else if (strncmp(token, "GENRE|", 6) == 0)  SET_METADATA(m.genre, token + 6, 511)
        else if (strncmp(token, "COMPOSER|", 9) == 0) SET_METADATA(m.composer, token + 9, 1023)
        else if (strncmp(token, "PLAYER|", 7) == 0) SET_METADATA(m.player, token + 7, 63)
        else if (strncmp(token, "ART|", 4) == 0) {
            char *art_val = token + 4;
            if (strncmp(art_val, "data:image", 10) == 0) {
                m.art_url[0] = '\0';
            } else {
                SET_METADATA(m.art_url, art_val, 4095);
            }
        }
        else if (strncmp(token, "LENGTH|", 7) == 0) {
            long long micro = atoll(token + 7);
            int seconds = (int)(micro / 1000000);
            snprintf(m.length_str, 31, "%d:%02d", seconds / 60, seconds % 60);
        }
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *line_start = token + 7;
            char *line_end;

            for (int i = 0; i < MAX_LYRIC_LINES; i++) {
                if (m.lyrics[i]) {
                    free(m.lyrics[i]);
                    m.lyrics[i] = NULL;
                }
            }
            m.lyric_count = 0;

            while ((line_end = strchr(line_start, '\n')) != NULL && m.lyric_count < MAX_LYRIC_LINES) {
                *line_end = '\0';
                size_t len = strlen(line_start);
                if (len > 0 && line_start[len-1] == '\r') line_start[len-1] = '\0';
                
                if (strlen(line_start) > 0) {
                    m.lyrics[m.lyric_count++] = strdup(line_start);
                }
                line_start = line_end + 1;
            }
            if (*line_start && m.lyric_count < MAX_LYRIC_LINES) {
                m.lyrics[m.lyric_count++] = strdup(line_start);
            }
        }

        current = next_sep + strlen(delim);
    }
}
