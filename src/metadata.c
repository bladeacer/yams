#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include "metadata.h"
#include "debug.h"
#include "utils.h"

Metadata m;

static char lyric_storage_bin[65536];

/* Hash of the last parsed lyrics payload. The metadata stream repeats the whole
 * format string on every update, so comparing hashes lets us reparse only when
 * the lyrics actually changed (or when a manual refresh was requested). */
static unsigned long long last_lyric_hash = 0;
static bool last_lyric_hash_valid = false;

/* Optimistic status handling for an instant pause/play response. */
static char pending_status[16];
static long pending_status_until = 0;
static bool pending_status_valid = false;

/* After an optimistic seek, ignore position samples that were emitted before
 * the player processed the seek. A close match is accepted and ends the guard. */
static long long seek_target_us = 0;
static long long seek_tolerance_us = 0;
static long seek_guard_until = 0;

static unsigned long long hash_text(const char *s) {
  unsigned long long h = 1469598103934665603ULL;
  if (!s) return h;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 1099511628211ULL;
  }
  return h;
}

void clear_metadata() {
  m.lyric_count = 0;
  memset(&m, 0, sizeof(Metadata));
  lyric_storage_bin[0] = '\0';
  last_lyric_hash = 0;
  last_lyric_hash_valid = false;
  pending_status[0] = '\0';
  pending_status_valid = false;
  pending_status_until = 0;
  seek_target_us = 0;
  seek_tolerance_us = 0;
  seek_guard_until = 0;
}

void metadata_request_refresh(void) {
  last_lyric_hash_valid = false;
}

static void copy_snap_field(char *dst, size_t size, const char *src) {
  if (size == 0) return;
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

void format_timestamp(long long us, char *buf, size_t size) {
  if (!buf || size == 0) return;
  if (us < 0) us = 0;
  long long total = us / 1000000;
  int hours = (int)(total / 3600);
  int minutes = (int)((total / 60) % 60);
  int seconds = (int)(total % 60);
  if (hours > 0)
    snprintf(buf, size, "%d:%02d:%02d", hours, minutes, seconds);
  else
    snprintf(buf, size, "%d:%02d", minutes, seconds);
}

void snapshot_metadata(MetadataSnap *snap) {
  if (!snap) return;
  memset(snap, 0, sizeof(*snap));
  snap->lyric_count = m.lyric_count;
  snap->length_us = m.length_us;
  snap->position_us = metadata_position_us();
  format_timestamp(snap->position_us, snap->position_str,
                   sizeof(snap->position_str));
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

long long metadata_position_us(void) {
  long long pos = m.position_us;
  if (pos < 0) pos = 0;

  if (strcmp(m.status, "Playing") == 0 && m.position_received_ms > 0) {
    long long elapsed = get_ms() - m.position_received_ms;
    if (elapsed > 0) {
      /* Avoid overflow if the clock jumps or the stream stalls for a long
       * time. 24h is far beyond anything LRC lyrics need. */
      if (elapsed > 24LL * 60 * 60 * 1000)
        elapsed = 24LL * 60 * 60 * 1000;
      pos += elapsed * 1000;
    }
  }

  if (m.length_us > 0 && pos > m.length_us)
    pos = m.length_us;
  return pos;
}

void metadata_apply_seek(long long delta_us) {
  long long pos = metadata_position_us() + delta_us;
  if (pos < 0)
    pos = 0;
  if (m.length_us > 0 && pos > m.length_us)
    pos = m.length_us;

  m.position_us = pos;
  m.position_received_ms = get_ms();

  long long magnitude = delta_us < 0 ? -delta_us : delta_us;
  seek_target_us = pos;
  seek_tolerance_us = magnitude / 2;
  if (seek_tolerance_us < 500000LL)
    seek_tolerance_us = 500000LL;
  seek_guard_until = get_ms() + 1500;
}

int metadata_lyric_index_at(long long position_us) {
  if (position_us < 0 || m.lyric_count <= 0)
    return -1;

  int best = -1;
  long long best_time = -1;
  for (int i = 0; i < m.lyric_count; i++) {
    long long t = m.lyric_times[i];
    if (t >= 0 && t <= position_us && t >= best_time) {
      best_time = t;
      best = i;
    }
  }
  return best;
}

int metadata_lyric_index(void) {
  return metadata_lyric_index_at(metadata_position_us());
}

void metadata_optimistic_toggle_status(void) {
  const char *next = (strcmp(m.status, "Playing") == 0) ? "Paused" : "Playing";

  /* Freeze the interpolated position before changing state so pausing does not
   * make the lyric highlight jump back to the last stream sample. */
  m.position_us = metadata_position_us();
  m.position_received_ms = get_ms();

  strncpy(m.status, next, sizeof(m.status) - 1);
  m.status[sizeof(m.status) - 1] = '\0';
  strncpy(pending_status, next, sizeof(pending_status) - 1);
  pending_status[sizeof(pending_status) - 1] = '\0';
  pending_status_valid = true;
  pending_status_until = get_ms() + 1000;
}

/* Parse one LRC timestamp starting at '['. Returns true and advances *end past
 * the closing ']' on success. Supports [mm:ss], [mm:ss.xx], [mm:ss.xxx] and
 * the older [mm:ss:xx] form. */
static bool parse_lrc_timestamp(const char *s, long long *out_us,
                                const char **end) {
  if (!s || *s != '[')
    return false;

  const char *p = s + 1;
  int minutes = 0;
  int digits = 0;
  while (isdigit((unsigned char)*p)) {
    minutes = minutes * 10 + (*p - '0');
    if (minutes > 100000) return false;
    p++;
    digits++;
  }
  if (digits == 0 || *p != ':')
    return false;

  p++;
  int seconds = 0;
  digits = 0;
  while (isdigit((unsigned char)*p)) {
    seconds = seconds * 10 + (*p - '0');
    if (seconds > 100000) return false;
    p++;
    digits++;
  }
  if (digits == 0)
    return false;

  long long fraction = 0;
  int frac_digits = 0;
  if (*p == '.' || *p == ':') {
    p++;
    while (isdigit((unsigned char)*p) && frac_digits < 6) {
      fraction = fraction * 10 + (*p - '0');
      p++;
      frac_digits++;
    }
    if (frac_digits == 0)
      return false;
  }
  if (*p != ']')
    return false;

  while (frac_digits < 6) {
    fraction *= 10;
    frac_digits++;
  }

  *out_us = ((long long)minutes * 60 + seconds) * 1000000LL + fraction;
  *end = p + 1;
  return true;
}

static void add_lyric_line(char *text, long long timestamp_us) {
  if (!text || m.lyric_count >= MAX_LYRIC_LINES)
    return;
  m.lyrics[m.lyric_count] = text;
  m.lyric_times[m.lyric_count] = timestamp_us;
  m.lyric_count++;
}

static void parse_lyrics_text(const char *raw) {
  m.lyric_count = 0;
  for (int i = 0; i < MAX_LYRIC_LINES; i++)
    m.lyric_times[i] = -1;

  if (!raw)
    return;

  strncpy(lyric_storage_bin, raw, sizeof(lyric_storage_bin) - 1);
  lyric_storage_bin[sizeof(lyric_storage_bin) - 1] = '\0';

  char *line = lyric_storage_bin;
  while (line && *line) {
    char *next = strchr(line, '\n');
    if (next)
      *next = '\0';

    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r')
      line[len - 1] = '\0';

    const char *cursor = line;
    long long timestamps[16];
    int timestamp_count = 0;

    while (timestamp_count < 16) {
      long long ts = 0;
      const char *after = NULL;
      if (!parse_lrc_timestamp(cursor, &ts, &after))
        break;
      timestamps[timestamp_count++] = ts;
      cursor = after;
    }

    if (timestamp_count > 0) {
      char *text = (char *)cursor;
      if (*text != '\0') {
        for (int i = 0; i < timestamp_count; i++)
          add_lyric_line(text, timestamps[i]);
      }
    } else if (line[0] != '[') {
      /* Plain (unsynced) lyric line. Bracketed non-timestamp tags such as
       * [ar:Artist] and [ti:Title] are metadata, not lyrics. */
      if (*line != '\0')
        add_lyric_line(line, -1);
    }

    if (!next)
      break;
    line = next + 1;
  }
}

bool parse_block(char *block) {
  if (!block || *block == '\0') return false;

  DBG("parse_block input: [%s]", block);

  bool song_changed = false;
  bool status_changed = false;
  bool lyrics_changed = false;
  bool art_changed = false;
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
            m.position_us = 0;
            m.position_received_ms = 0;
            last_lyric_hash_valid = false;
          }
          break;

        case 'A': // ARTIST, ALBUM, ART
          if (key[1] == 'R' && key[2] == 'T' && key[3] == '\0') { // ART
            DBG("  ART field: val=[%s] (len=%zu), file_path=[%s]", val, strlen(val), m.file_path);
            if (val[0] == '@' && val[1] == '\0') {
              if (strlen(m.file_path) > 5 && !is_remote_url(m.file_path)) {
                if (strcmp(m.art_url, m.file_path) != 0)
                  art_changed = true;
                strncpy(m.art_url, m.file_path, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
                DBG("  ART -> @ sentinel, copied file_path to art_url: [%s]", m.art_url);
              } else {
                /* Drop art from the previous track instead of showing it for
                 * a remote track that has no usable artwork. */
                if (m.art_url[0]) {
                  m.art_url[0] = '\0';
                  art_changed = true;
                }
                DBG("  ART -> @ sentinel but file_path is missing or remote, art_url cleared");
              }
            } else if (val[0] != 'd' || val[1] != 'a') {
              if (strcmp(m.art_url, val) != 0)
                art_changed = true;
              strncpy(m.art_url, val, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
              DBG("  ART -> direct url: [%s]", m.art_url);
            } else if (strncmp(val, "data:image/", 11) == 0) {
              if (strlen(m.file_path) > 5 && !is_remote_url(m.file_path)) {
                if (strcmp(m.art_url, m.file_path) != 0)
                  art_changed = true;
                strncpy(m.art_url, m.file_path, sizeof(m.art_url) - 1); m.art_url[sizeof(m.art_url) - 1] = '\0';
                DBG("  ART -> data:image, copied file_path to art_url: [%s]", m.art_url);
              } else {
                if (m.art_url[0]) {
                  m.art_url[0] = '\0';
                  art_changed = true;
                }
                DBG("  ART -> data:image but file_path is missing or remote, art_url cleared");
              }
            }
          } else if (key[2] == 'T') { // ARTIST
            if (val[0] && strcmp(m.artist, val) != 0) {
              strncpy(m.artist, val, sizeof(m.artist) - 1); m.artist[sizeof(m.artist) - 1] = '\0';
              song_changed = true;
              m.lyric_count = 0;
              m.position_us = 0;
              m.position_received_ms = 0;
              last_lyric_hash_valid = false;
            }
          } else if (key[2] == 'B') { // ALBUM
            strncpy(m.album, val, sizeof(m.album) - 1); m.album[sizeof(m.album) - 1] = '\0';
          }
          break;

        case 'S': // STATUS
          {
            bool accept = true;
            if (pending_status_valid) {
              if (strcmp(val, pending_status) == 0) {
                pending_status_valid = false;
              } else if (get_ms() < pending_status_until) {
                accept = false;
              } else {
                pending_status_valid = false;
              }
            }
            if (accept && strcmp(m.status, val) != 0) {
              /* Preserve the interpolated position across state changes. */
              m.position_us = metadata_position_us();
              m.position_received_ms = get_ms();
              strncpy(m.status, val, sizeof(m.status) - 1); m.status[sizeof(m.status) - 1] = '\0';
              status_changed = true;
            }
          }
          break;

        case 'L': // LENGTH, LYRICS
          if (key[1] == 'E') { // LENGTH
            long long micro = atoll(val);
            m.length_us = micro;
            format_timestamp(micro, m.length_str, sizeof(m.length_str));
          } else if (key[1] == 'Y') { // LYRICS
            unsigned long long h = hash_text(val);
            if (song_changed || !last_lyric_hash_valid || h != last_lyric_hash) {
              parse_lyrics_text(val);
              last_lyric_hash = h;
              last_lyric_hash_valid = true;
              lyrics_changed = true;
            }
          }
          break;

        case 'P': // PLAYER, PATH, POSITION
          if (strcmp(key, "POSITION") == 0) {
            /* Unsupported position templates expand to an empty string. */
            if (val[0]) {
              long long new_pos = atoll(val);
              if (new_pos < 0) new_pos = 0;

              /* A block emitted before the player handled an optimistic seek
               * would otherwise snap the UI back to the old position. */
              bool stale =
                  seek_guard_until > 0 && get_ms() < seek_guard_until &&
                  llabs(new_pos - seek_target_us) > seek_tolerance_us;
              if (!stale) {
                m.position_us = new_pos;
                m.position_received_ms = get_ms();
                seek_guard_until = 0;
              }
            }
          } else if (key[1] == 'L') { // PLAYER
            strncpy(m.player, val, sizeof(m.player) - 1); m.player[sizeof(m.player) - 1] = '\0';
            DBG("  PLAYER = [%s]", m.player);
          } else if (strcmp(key, "PATH") == 0) {
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
  return (song_changed || status_changed || lyrics_changed || art_changed);
}
