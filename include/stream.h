#ifndef STREAM_H
#define STREAM_H

#include <stdbool.h>
#include <stddef.h>

/* Replace complete or chunk-split "\tART|data:image..." fields with "\tART|@".
 * *skip carries the "we are inside a data URI that crossed a chunk boundary"
 * state between calls. *len is updated to the remaining buffer length. */
void stream_strip_data_art(char *buf, size_t *len, bool *skip);

/* Append a chunk to a fixed-size NUL-terminated stream buffer, trimming at the
 * first "[[END]]" marker (or resetting) when the buffer would overflow.
 * Returns the new used length. */
size_t stream_buffer_append(char *buffer, size_t capacity, size_t used,
                            const char *chunk, size_t chunk_len);

#endif
