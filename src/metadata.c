#include <string.h>
#include <stdlib.h>
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
}

void parse_block(char *block) {
    clear_metadata();

    char *current = block;
    char *next_sep;
    const char *delim = "¶";
    
    while ((next_sep = strstr(current, delim)) != NULL) {
        *next_sep = '\0';
        char *token = current;

        #define SET_METADATA(field, offset, len) \
            { strncpy(field, token + offset, len); field[len] = '\0'; }

        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "ALBUM|", 6) == 0) strncpy(m.album, token + 6, 511);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        else if (strncmp(token, "PATH|", 5) == 0) strncpy(m.file_path, token + 5, 4095);
        else if (strncmp(token, "GENRE|", 6) == 0) strncpy(m.genre, token + 6, 511);
        else if (strncmp(token, "COMPOSER|", 9) == 0) strncpy(m.composer, token + 9, 1023);
        if (strncmp(token, "PLAYER|", 7) == 0) {
            strncpy(m.player, token + 7, sizeof(m.player) - 1);
            m.player[sizeof(m.player) - 1] = '\0';
        }
        else if (strncmp(token, "ART|", 4) == 0) {
            char *art_val = token + 4;
            if (strncmp(art_val, "data:image", 10) == 0) {
                m.art_url[0] = '\0';
            } else {
                strncpy(m.art_url, art_val, 4095);
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
            while ((line_end = strchr(line_start, '\n')) != NULL && m.lyric_count < MAX_LYRIC_LINES) {
                *line_end = '\0';
                m.lyrics[m.lyric_count++] = strdup(line_start);
                line_start = line_end + 1;
            }
            if (*line_start && m.lyric_count < MAX_LYRIC_LINES) {
                m.lyrics[m.lyric_count++] = strdup(line_start);
            }
        }
        current = next_sep + strlen(delim);
    }
}
