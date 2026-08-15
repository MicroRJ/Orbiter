#ifndef ORB_H
#define ORB_H

#include "nes/emulator.h"

// Loads an iNES file into arena-owned memory. Game data and the optional title
// remain valid until the arena is reset. Failure leaves the arena unchanged.
NES_Game *orb_game_from_ines_file(Arena *arena, Str path, Str *title);
Hash256 orb_game_hash(NES_Game game);

#endif
