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

static void copy_snap_field(char *dst, size_t size, const char *src) {
    if (size == 0) return;
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

void snapshot_metadata(MetadataSnap *snap) {
    if (!snap) return;
    memset(snap, 0, sizeof(*snap));
    snap->lyric_count = m.lyric_count;
    copy_snap_field(snap->status, sizeof(snap->status), m.status);
    copy_snap_field(snap->player, sizeof(snap->player), m.player);
    copy_snap_field(snap->length_str, sizeof(snap->length_str), m.length_str);
    copy_snap_field(snap->title, sizeof(snap->title), m.title);
    copy_snap_field(snap->artist, sizeof(snap->artist), m.artist);
    copy_snap_field(snap->album, sizeof(snap->album), m.album);
    copy_snap_field(snap->genre, sizeof(snap->genre), m.genre);
    copy_snap_field(snap->composer, sizeof(snap->composer), m.composer);
    copy_snap_field(snap->art_url, sizeof(snap->art_url), m.art_url);
    copy_snap_field(snap->file_path, sizeof(snap->file_path), m.file_path);
}

bool parse_block(char *block) {
    if (!block || *block == '\0') return false;

    DBG("parse_block input: [%s]", block);

    bool song_changed = false;
    bool status_changed = false;
    char *saveptr_outer;
    
    char *token = strtok_r(block, "\t", &saveptr_outer);
    
    while (token) {
        char *sep = strchr(token, '|');
        if (sep) {
            *sep = '\0';
            char *key = token;
            char *val = sep + 1;

            DBG("  token: %s = %s", key, val);

            switch (key[0]) {
                case 'T': // TITLE
                    if (val[0] && strcmp(m.title, val) != 0) {
                        strncpy(m.title, val, sizeof(m.title) - 1); m.title[sizeof(m.title) - 1] = '\0';
                        song_changed = true;
                        m.lyric_count = 0;
                    }
                    break;

                case 'A': // ARTIST, ALBUM, ART
                    if (key[1] == 'R' && key[2] == 'T' && key[3] == '\0') { // ART
                        DBG("  ART field: val=[%s] (len=%zu), file_path=[%s]", val, strlen(val), m.file_path);
                        if (val[0] == '@' && val[1] == '\0') {
                            if (strlen(m.file_path) > 5) {
                                strncpy(m.art_url, m.file_path, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
                                DBG("  ART -> @ sentinel, copied file_path to art_url: [%s]", m.art_url);
                            } else {
                                DBG("  ART -> @ sentinel but file_path too short, art_url unchanged");
                            }
                        } else if (val[0] != 'd' || val[1] != 'a') {
                            strncpy(m.art_url, val, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
                            DBG("  ART -> direct url: [%s]", m.art_url);
                        } else if (strncmp(val, "data:image/", 11) == 0) {
                            if (strlen(m.file_path) > 5) {
                                strncpy(m.art_url, m.file_path, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
                                DBG("  ART -> data:image, copied file_path to art_url: [%s]", m.art_url);
                            } else {
                                DBG("  ART -> data:image but file_path too short, art_url unchanged");
                            }
                        }
                    } else if (key[2] == 'T') { // ARTIST
                        if (val[0] && strcmp(m.artist, val) != 0) {
                            strncpy(m.artist, val, sizeof(m.artist) - 1); m.artist[sizeof(m.artist) - 1] = '\0';
                            song_changed = true;
                        }
                    } else if (key[2] == 'B') { // ALBUM
                        strncpy(m.album, val, sizeof(m.album) - 1); m.album[sizeof(m.album) - 1] = '\0';
                    }
                    break;

                case 'S': // STATUS
                    if (strcmp(m.status, val) != 0) {
                        strncpy(m.status, val, sizeof(m.status) - 1); m.status[sizeof(m.status) - 1] = '\0';
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
                        strncpy(lyric_storage_bin, val, sizeof(lyric_storage_bin) - 1); lyric_storage_bin[sizeof(lyric_storage_bin) - 1] = '\0';
                        
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
                        strncpy(m.player, val, sizeof(m.player) - 1); m.player[sizeof(m.player) - 1] = '\0';
                        DBG("  PLAYER = [%s]", m.player);
                    } else { // PATH
                        strncpy(m.file_path, val, sizeof(m.file_path) - 1); m.file_path[sizeof(m.file_path) - 1] = '\0';
                        DBG("  PATH = [%s]", m.file_path);
                    }
                    break;

                case 'G': // GENRE
                    strncpy(m.genre, val, sizeof(m.genre) - 1); m.genre[sizeof(m.genre) - 1] = '\0';
                    break;

                case 'C': // COMPOSER
                    strncpy(m.composer, val, sizeof(m.composer) - 1); m.composer[sizeof(m.composer) - 1] = '\0';
                    break;
            }
        } else {
            DBG("  token (no sep): [%s]", token);
        }
        token = strtok_r(NULL, "\t", &saveptr_outer);
    }
    
    DBG("  parse_block done: art_url=[%s] file_path=[%s] title=[%s] artist=[%s] status=[%s]",
        m.art_url, m.file_path, m.title, m.artist, m.status);
    return (song_changed || status_changed);
}
