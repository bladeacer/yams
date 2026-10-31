#ifndef PLAYER_BACKEND_H
#define PLAYER_BACKEND_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
  int (*play_pause)(void);
  int (*next)(void);
  int (*prev)(void);
  int (*seek)(const char *param); // e.g. "5+" or "5-"

  // start a metadata-streaming subprocess; returns pid, writes fd to *out_fd
  pid_t (*spawn_stream)(const char *fmt, int *out_fd);
  bool (*has_player)(void);

  // parse raw bytes from the stream; returns true if metadata changed.
  // NULL means main.c's legacy playerctl-format parsing is used.
  bool (*parse_chunk)(const char *data, size_t n);

  // notification from the main-loop SIGCHLD reaper
  void (*reap_child)(pid_t pid);
  void (*cleanup)(void); // kills pending ctrl process, frees resources
} PlayerBackend;

PlayerBackend *player_backend_init(void);

#endif
