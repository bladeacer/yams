#include "player_backend/player_backend.h"
#include "player_backend/player_backend_mpris.h"

PlayerBackend *player_backend_init(void) { return mpris_backend_init(); }
