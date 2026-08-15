#ifndef ORB_H
#define ORB_H

#include "nes/emulator.h"

typedef struct
{
	u32 mapper;
	b32 vmirror;
	b32 has_trainer;
	b32 four_screen;
	u32 prg_rom_size;
	u32 chr_rom_size;
}
Orb_GameMetadata;

typedef struct
{
	Orb_GameMetadata metadata;
	u8 *prg_rom_data;
	u8 *chr_rom_data;
	u8 *trainer_data;
}
Orb_Game;

// Loads an iNES file into arena-owned memory. Game data and the optional title
// remain valid until the arena is reset. Failure leaves the arena unchanged.
Orb_Game *orb_game_from_ines_file(Arena *arena, Str path, Str *title);
Hash256 orb_game_hash(Orb_Game game);

#endif
