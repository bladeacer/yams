#include "player_backend/player_backend.h"
#if defined(__linux__)
#include "player_backend/player_backend_mpris.h"
PlayerBackend *player_backend_init(void) { return mpris_backend_init(); }
#elif defined(__APPLE__)
#include "player_backend/player_backend_macos.h"
PlayerBackend *player_backend_init(void) { return NULL; }
#endif
