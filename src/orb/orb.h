#ifndef ORB_H
#define ORB_H

#include "nes/emulator.h"

typedef enum
{
	ORB_PIXEL_FORMAT_RGBA8 = 1,
}
Orb_PixelFormat;

typedef struct
{
	u32 width;
	u32 height;
	u32 stride;
	Orb_PixelFormat format;
	ByteSpan pixels;
}
Orb_Thumbnail;

typedef struct
{
	u64 scheduler_clock;
	u64 sample_phase;
	u8 values[32];
	NES_InputState input_state;
	u32 cpu_stall_cycles;
	NES_CPUState cpu;
	NES_PPUState ppu;
	NES_APUState apu;
	u8 controllers[2];
	u8 wram[NES_WRAM_SIZE];
	u8 vram[NES_VRAM_SIZE];
	u8 chr_ram[NES_MAX_CHR_RAM_SIZE];
	u8 prg_ram[NES_MAX_PRG_RAM_SIZE];
	u8 video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
}
Orb_SaveState;

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
void orb_capture_save_state(Orb_SaveState *save, const NES_Emulator *emulator);
void orb_restore_save_state(NES_Emulator *emulator, const Orb_SaveState *save);

#endif
