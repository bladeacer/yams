#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "metadata.h"
#include "debug.h"

Metadata m;

void clear_metadata() {
    m.lyric_count = 0;
    memset(&m, 0, sizeof(Metadata));
}

bool parse_block(char *block) {
    if (!block || *block == '\0') return false;

    bool song_changed = false;
    bool status_changed = false;
    char *current = block;
    char *next_sep;
    const char *delim = "#";

    while ((next_sep = strstr(current, delim)) != NULL) {
        *next_sep = '\0';
        char *token = current;
        
        if (*token == '\0') {
            current = next_sep + 1;
            continue;
        }

        if (strncmp(token, "PLAYER|", 7) == 0) {
            strncpy(m.player, token + 7, 63);
        } else if (strncmp(token, "STATUS|", 7) == 0) {
            char *val = token + 7;
            if (strcmp(m.status, val) != 0) {
                strncpy(m.status, val, 63);
                status_changed = true;
            }
        } else if (strncmp(token, "TITLE|", 6) == 0) {
            char *val = token + 6;
            if (strcmp(m.title, val) != 0) {
                strncpy(m.title, val, 511);
                song_changed = true;
            }
        } else if (strncmp(token, "ARTIST|", 7) == 0) {
            char *val = token + 7;
            if (strcmp(m.artist, val) != 0) {
                strncpy(m.artist, val, 511);
                song_changed = true;
            }
        } else if (strncmp(token, "ALBUM|", 6) == 0) {
            strncpy(m.album, token + 6, 511);
        } else if (strncmp(token, "PATH|", 5) == 0) {
            strncpy(m.file_path, token + 5, 4095);
        } else if (strncmp(token, "LENGTH|", 7) == 0) {
            long long micro = atoll(token + 7);
            int seconds = (int)(micro / 1000000);
            snprintf(m.length_str, 31, "%d:%02d", seconds / 60, seconds % 60);
        } else if (strncmp(token, "ART|", 4) == 0) {
            char *val = token + 4;
            if (!(val[0] == 'd' && val[1] == 'a' && strncmp(val, "data:image", 10) == 0)) {
                strncpy(m.art_url, val, 4095);
            }
        } else if (strncmp(token, "LYRICS|", 7) == 0 && song_changed) {
            m.lyric_count = 0;
            strncpy(m.lyric_buffer, token + 7, sizeof(m.lyric_buffer) - 1);
            char *line = m.lyric_buffer;
            while (line && *line && m.lyric_count < MAX_LYRIC_LINES) {
                char *next_line = strchr(line, '\n');
                if (next_line) *next_line = '\0';
                if (*line != '\0' && *line != '\r') m.lyrics[m.lyric_count++] = line;
                line = next_line ? next_line + 1 : NULL;
            }
        }

        current = next_sep + 1;
    }
    return (song_changed || status_changed);
}
