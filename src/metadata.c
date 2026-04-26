#include <string.h>
#include <stdlib.h>
#include "metadata.h"

Metadata m;

void clear_metadata() {
    for (int i = 0; i < m.lyric_count; i++) {
        if (m.lyrics[i]) free(m.lyrics[i]);
    }
    memset(&m, 0, sizeof(Metadata));
}

void parse_block(char *block) {
    clear_metadata();
    char *saveptr;
    char *token = strtok_r(block, "¶", &saveptr);
    while (token) {
        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "ALBUM|", 6) == 0) strncpy(m.album, token + 6, 511);
        else if (strncmp(token, "GENRE|", 6) == 0) strncpy(m.genre, token + 6, 255);
        else if (strncmp(token, "COMP|", 5) == 0) strncpy(m.composer, token + 5, 511);
        else if (strncmp(token, "PATH|", 5) == 0) strncpy(m.file_path, token + 5, 4095);
        else if (strncmp(token, "PLAYER|", 7) == 0) strncpy(m.player, token + 7, 63);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        else if (strncmp(token, "ART|", 4) == 0) strncpy(m.art_url, token + 4, 4095);
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *lyr_data = strdup(token + 7);
            char *l_saveptr, *line = strtok_r(lyr_data, "\n", &l_saveptr);
            while (line && m.lyric_count < MAX_LYRIC_LINES) {
                m.lyrics[m.lyric_count++] = strdup(line);
                line = strtok_r(NULL, "\n", &l_saveptr);
            }
            free(lyr_data);
        }
        token = strtok_r(NULL, "¶", &saveptr);
    }
}
