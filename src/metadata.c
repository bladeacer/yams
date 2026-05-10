#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "metadata.h"
#include "debug.h"

Metadata m;

static char lyric_storage_bin[65536];

void clear_metadata() {
    m.lyric_count = 0;
    memset(&m, 0, sizeof(Metadata));
    lyric_storage_bin[0] = '\0';
}

bool parse_block(char *block) {
    if (!block || *block == '\0') return false;

    bool song_changed = false;
    bool status_changed = false;
    char *saveptr_outer;
    
    char *token = strtok_r(block, "#", &saveptr_outer);
    
    while (token) {
        char *sep = strchr(token, '|');
        if (sep) {
            *sep = '\0';
            char *key = token;
            char *val = sep + 1;

            switch (key[0]) {
                case 'T': // TITLE
                    if (strcmp(m.title, val) != 0) {
                        strncpy(m.title, val, sizeof(m.title) - 1);
                        song_changed = true;
                        m.lyric_count = 0;
                    }
                    break;

                case 'A': // ARTIST, ALBUM, ART
                    if (key[1] == 'R' && key[2] == 'T' && key[3] == '\0') { // ART
                        if (val[0] != 'd' || val[1] != 'a') {
                            strncpy(m.art_url, val, sizeof(m.art_url) - 1);
                        }
                    } else if (key[2] == 'T') { // ARTIST
                        if (strcmp(m.artist, val) != 0) {
                            strncpy(m.artist, val, sizeof(m.artist) - 1);
                            song_changed = true;
                        }
                    } else if (key[2] == 'B') { // ALBUM
                        strncpy(m.album, val, sizeof(m.album) - 1);
                    }
                    break;

                case 'S': // STATUS
                    if (strcmp(m.status, val) != 0) {
                        strncpy(m.status, val, sizeof(m.status) - 1);
                        status_changed = true;
                    }
                    break;

                case 'L': // LENGTH, LYRICS
                    if (key[1] == 'E') { // LENGTH
                        long long micro = atoll(val);
                        int seconds = (int)(micro / 1000000);
                        snprintf(m.length_str, sizeof(m.length_str), "%d:%02d", seconds / 60, seconds % 60);
                    } else if (key[1] == 'Y' && song_changed) { // LYRICS
                        m.lyric_count = 0;
                        strncpy(lyric_storage_bin, val, sizeof(lyric_storage_bin) - 1);
                        
                        char *line_saveptr;
                        char *line = strtok_r(lyric_storage_bin, "\n", &line_saveptr);
                        while (line && m.lyric_count < MAX_LYRIC_LINES) {
                            size_t len = strlen(line);
                            if (len > 0 && line[len-1] == '\r') line[len-1] = '\0';
                            
                            if (*line != '\0') {
                                m.lyrics[m.lyric_count++] = line;
                            }
                            line = strtok_r(NULL, "\n", &line_saveptr);
                        }
                    }
                    break;

                case 'P': // PLAYER, PATH
                    if (key[1] == 'L') { // PLAYER
                        strncpy(m.player, val, sizeof(m.player) - 1);
                    } else { // PATH
                        strncpy(m.file_path, val, sizeof(m.file_path) - 1);
                    }
                    break;

                case 'G': // GENRE
                    strncpy(m.genre, val, sizeof(m.genre) - 1);
                    break;

                case 'C': // COMPOSER
                    strncpy(m.composer, val, sizeof(m.composer) - 1);
                    break;
            }
        }
        token = strtok_r(NULL, "#", &saveptr_outer);
    }
    
    return (song_changed || status_changed);
}
