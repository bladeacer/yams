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
  int (*get_metadata)(TrackInfo *out);
  int (*get_status)(PlaybackStatus *out);
  int (*set_volume)(float v);
  void (*cleanup)(void);
} PlayerBackend;

PlayerBackend *player_backend_init(void); // ret nullptr on fail, compile w c23
