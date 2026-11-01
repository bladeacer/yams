#ifndef ART_H
#define ART_H

#include <chafa.h>
#include <stdbool.h>

char *render_art_to_buffer(int x, int y, int w, int h,
                            ChafaSymbolTags tags, ChafaPixelMode pixel_mode, int is_ascii);

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

#endif
