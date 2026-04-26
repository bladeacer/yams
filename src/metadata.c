#include <string.h>
#include <stdlib.h>
#include "metadata.h"
#include "debug.h"

Metadata m;

void clear_metadata() {
    for (int i = 0; i < m.lyric_count; i++) {
        if (m.lyrics[i]) free(m.lyrics[i]);
    }
    m.lyric_count = 0;
    memset(&m, 0, sizeof(Metadata));
}

void parse_block(char *block) {
    clear_metadata();

    char *current = block;
    char *next_sep;
    const char *delim = "¶";
    
    while ((next_sep = strstr(current, delim)) != NULL) {
        *next_sep = '\0';
        char *token = current;

        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "ALBUM|", 6) == 0) strncpy(m.album, token + 6, 511);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        else if (strncmp(token, "PLAYER|", 7) == 0) strncpy(m.player, token + 7, 63);
        else if (strncmp(token, "PATH|", 5) == 0) strncpy(m.url, token + 5, 4095);
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *line_start = token + 7;
            char *line_end;
            // Preserves empty lines by searching for \n one by one
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
