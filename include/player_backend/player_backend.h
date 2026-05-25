#pragma once
#include <stdbool.h>
#include <sys/types.h>

// pattern: ifdef for linux or macos, player.c inits the struct depending on OS
typedef struct {
  char *title;
  char *artist;
  char *album;
  float volume;
  int duration_ms;
  int position_ms;
} TrackInfo;

typedef enum {
  STATUS_PLAYING,
  STATUS_PAUSED,
  STATUS_STOPPED,
} PlaybackStatus;

typedef struct {
  int (*play_pause)(void);
  int (*next)(void);
  int (*prev)(void);
  int (*seek)(const char *param); // e.g. "5+" or "5-"

  // start a metadata-streaming subprocess; returns pid, writes fd to *out_fd
  pid_t (*spawn_stream)(const char *fmt, int *out_fd);
  bool (*has_player)(void);

  // future non-playerctl backends
  int (*get_metadata)(TrackInfo *out);
  int (*get_status)(PlaybackStatus *out);
  int (*set_volume)(float v);
  void (*cleanup)(void); // kills pending ctrl process, frees resources
} PlayerBackend;

PlayerBackend *player_backend_init(void);
