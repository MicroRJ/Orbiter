#ifndef NES_GAME_H
#define NES_GAME_H

#include "base.h"

typedef enum
{
	NES_MIRROR_HORIZONTAL = 1,
	NES_MIRROR_VERTICAL,
	NES_MIRROR_FOUR_SCREEN,
}
NES_Mirroring;

typedef struct
{
	u32              mapper;
	NES_Mirroring mirroring;
	u32        trainer_size;
	u32        prg_rom_size;
	u32        chr_rom_size;
}
NES_GameMetadata;

typedef struct
{
	NES_GameMetadata metadata;
	const u8          *trainer;
	const u8          *prg_rom;
	const u8          *chr_rom;
}
NES_Game;

#endif
