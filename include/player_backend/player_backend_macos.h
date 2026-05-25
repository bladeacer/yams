typedef struct {
  char title[256];
  char artist[256];
  char album[256];
  double duration;
  double elapsed;
  float playback_rate; // 0.0 = paused, 1.0 = playing
} MRTrackInfo;

// returns 0 on success, -1 on failure/timeout
int mr_get_now_playing(MRTrackInfo *out);
int mr_send_command(int cmd); // 0=play, 1=pause, 2=toggle, 3=next, 4=prev
