#include "stream.h"
#include "debug.h"
#include <string.h>

#define ART_MARKER "\tART|data:image"
#define ART_MARKER_LEN 15
#define END_MARKER "[[END]]"
#define END_MARKER_LEN 7

void stream_strip_data_art(char *buf, size_t *len, bool *skip) {
  if (!buf || !len)
    return;

  size_t n = *len;

  if (skip && *skip) {
    char *tab = n ? memchr(buf, '\t', n) : NULL;
    if (!tab) {
      *len = 0;
      return;
    }
    size_t off = (size_t)(tab - buf);
    memmove(buf, tab, n - off);
    n -= off;
    *skip = false;
  }

  size_t off = 0;
  while (n >= ART_MARKER_LEN && off <= n - ART_MARKER_LEN) {
    char *p = (char *)memmem(buf + off, n - off, ART_MARKER, ART_MARKER_LEN);
    if (!p)
      break;

    size_t pos = (size_t)(p - buf);
    char *val = p + 5;
    size_t vrem = n - (pos + 5);
    char *tab = vrem ? memchr(val, '\t', vrem) : NULL;

    if (tab) {
      size_t ulen = (size_t)(tab - val);
      size_t after = n - (pos + 5 + ulen);
      val[0] = '@';
      if (after > 0)
        memmove(val + 1, tab, after);
      n = ulen > 0 ? n - (ulen - 1) : pos + 6;
      off = pos + 6;
      DBG("  stripped data URI at %zu, chunk now %zu bytes", pos, n);
    } else {
      buf[pos + 5] = '@';
      n = pos + 6;
      if (skip)
        *skip = true;
      DBG("  truncated data URI at %zu, chunk now %zu, skip set", pos, n);
      break;
    }
  }

  *len = n;
}

size_t stream_buffer_append(char *buffer, size_t capacity, size_t used,
                            const char *chunk, size_t chunk_len) {
  if (!buffer || capacity == 0)
    return 0;

  if (used > capacity - 1)
    used = 0;
  if (chunk_len > capacity - 1)
    chunk_len = capacity - 1;

  if (used + chunk_len >= capacity - 1) {
    DBG("OVERFLOW: used=%zu, incoming=%zu, capacity=%zu", used, chunk_len,
        capacity);

    char *first = used ? memmem(buffer, used, END_MARKER, END_MARKER_LEN) : NULL;
    if (first) {
      size_t offset = (size_t)(first - buffer) + END_MARKER_LEN;
      size_t trailing = used > offset ? used - offset : 0;
      memmove(buffer, buffer + offset, trailing);
      used = trailing;
      DBG("  trimmed at [[END]], used now %zu", used);
    } else {
      DBG("  no [[END]], resetting buffer");
      used = 0;
    }
    buffer[used] = '\0';

    if (used + chunk_len >= capacity - 1) {
      DBG("  still overflows, full reset");
      used = 0;
      buffer[0] = '\0';
    }
  }

  if (chunk_len > capacity - 1 - used)
    chunk_len = capacity - 1 - used;

  memcpy(buffer + used, chunk, chunk_len);
  used += chunk_len;
  buffer[used] = '\0';
  return used;
}
