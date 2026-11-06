#ifndef ART_H
#define ART_H

#include <chafa.h>
#include <stdbool.h>

char *render_art_to_buffer(int x, int y, int w, int h,
                            ChafaSymbolTags tags, ChafaPixelMode pixel_mode,
                            int is_ascii);

/* Same as render_art_to_buffer(), but when refresh_remote is true a remote
 * source is re-downloaded even on a cache hit. The fresh copy only replaces
 * the cached file when it decodes and has a larger pixel area, so a low
 * resolution retry can never downgrade existing artwork. */
char *render_art_to_buffer_ex(int x, int y, int w, int h,
                              ChafaSymbolTags tags, ChafaPixelMode pixel_mode,
                              int is_ascii, bool refresh_remote);

/* Dimensions of the last successfully decoded image, before terminal fitting
 * or content cropping. Both are zero when no image is loaded. */
void art_last_image_size(int *width, int *height);

/* True when the last decoded image is small enough to be a thumbnail. Remote
 * artwork at this size is worth refreshing in case a larger copy appears. */
bool art_last_image_is_low_res(void);

/* True when the artwork for this source is already available locally, so it
 * can be rendered without waiting for the art debounce. */
bool art_source_is_cached(const char *source);

/* Best artwork source for the current track, or NULL when there is none.
 * A local media path is used for embedded art; a remote track URL (a stream
 * page) is not artwork and must not be downloaded. */
const char *art_source_for_current_track(void);

/* Terminal capability guard.
 *
 * art_mode_ready() reports whether the active terminal can actually display
 * the given pixel mode. It also prepares terminal state that can be enabled
 * on demand (currently tmux's allow-passthrough window option). Results are
 * cached for the lifetime of the process.
 *
 * When a mode is unavailable, art_mode_unsupported_hint() returns a short
 * human-readable reason. art_fallback_mode() picks the best art-mode index
 * the terminal does support, falling back to plain symbols.
 *
 * art_mode_cleanup() restores any terminal state changed by
 * art_mode_ready(). Call it once on exit. */
bool art_mode_ready(ChafaPixelMode pixel_mode);
const char *art_mode_unsupported_hint(ChafaPixelMode pixel_mode);
int art_fallback_mode(int desired_mode);
void art_mode_cleanup(void);

char *get_cache_info(void);
char *clear_art_cache(void);
void art_cache_bump(void);

/* Monotonically increasing counter, bumped whenever cache contents change.
 * The UI uses it to know when the cache size label needs refreshing. */
int art_cache_version(void);

#endif
