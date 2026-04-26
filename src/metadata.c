#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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
    // Split by our custom delimiter
    char *token = strtok_r(block, "¶", &saveptr);

    while (token) {
        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "ALBUM|", 6) == 0) strncpy(m.album, token + 6, 511);
        else if (strncmp(token, "PLAYER|", 7) == 0) strncpy(m.player, token + 7, 63);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        else if (strncmp(token, "PATH|", 5) == 0) strncpy(m.file_path, token + 5, 4095);
        else if (strncmp(token, "ART|", 4) == 0) strncpy(m.art_url, token + 4, 4095);
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            char *raw_lyrics = token + 7;
            
            // Skip the "tauon xesam:asText" junk if it exists
            char *header = strstr(raw_lyrics, "xesam:asText");
            if (header) raw_lyrics = header + strlen("xesam:asText");

            char *lyr_ptr = strdup(raw_lyrics);
            char *to_free = lyr_ptr;
            char *line;

            // Use strsep to keep \n\n as empty lines
            while ((line = strsep(&lyr_ptr, "\n")) != NULL && m.lyric_count < MAX_LYRIC_LINES) {
                // Trim trailing \r (CRLF issues)
                size_t len = strlen(line);
                if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';
                
                m.lyrics[m.lyric_count++] = strdup(line);
            }
            free(to_free);
        }
        token = strtok_r(NULL, "¶", &saveptr);
    }
}
