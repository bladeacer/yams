#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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
    m.title[0] = '\0'; 
}

void parse_block(char *block) {
    // 1. Clear old data
    clear_metadata();

    // 2. Parse New Data
    char *current = block;
    char *next_sep;
    const char *delim = "¶";
    
    while ((next_sep = strstr(current, delim)) != NULL) {
        *next_sep = '\0';
        char *token = current;

        if (strncmp(token, "TITLE|", 6) == 0) strncpy(m.title, token + 6, 511);
        else if (strncmp(token, "ARTIST|", 7) == 0) strncpy(m.artist, token + 7, 511);
        else if (strncmp(token, "STATUS|", 7) == 0) strncpy(m.status, token + 7, 31);
        // Map PATH (xesam:url) to art_url for the renderer
        else if (strncmp(token, "PATH|", 5) == 0) strncpy(m.art_url, token + 5, 4095);
        // Map any multiline text to lyrics
        else if (strncmp(token, "LYRICS|", 7) == 0) {
            // Logic to split token + 7 into m.lyrics lines would go here
        }

        current = next_sep + strlen(delim);
    }
    DBG("PARSER: Updated metadata for '%s'", m.title);
}
