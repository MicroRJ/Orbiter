#ifndef ORB_H
#define ORB_H

#include "nes/emulator.h"

typedef struct Orb Orb;

typedef enum
{
	ORB_SAVE_RESUME = 1,
	ORB_SAVE_MANUAL,
}
Orb_SaveKind;

typedef enum
{
	ORB_PIXEL_FORMAT_RGBA8 = 1,
}
Orb_PixelFormat;

typedef struct
{
	u8 bytes[16];
}
Orb_Id;

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
	u64 kind;
	u64 flags;
	Orb_Id id;
	u64 created_unix_ms;
	u64 updated_unix_ms;
	u64 play_time_ms;
}
Orb_SaveMetadata;

typedef struct Orb_SaveNode Orb_SaveNode;
struct Orb_SaveNode
{
	Orb_SaveNode *next;
	Orb *orb;
	Orb_SaveMetadata metadata;
	Orb_Thumbnail thumbnail;
	Orb_SaveState state;
};

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

typedef struct Orb Orb;
struct Orb
{
	Str disk_path;
	Orb_Game game;
	Hash256 game_hash;
	Str title;
	u64 first_played_unix_ms;
	u64 last_played_unix_ms;
	u64 play_time_ms;
	Orb_SaveNode *first_save;
	Orb_SaveNode *last_save;
	u32 save_count;
};

typedef struct
{
	Arena arena;
	Str path;
	Orb *orb;
}
Orb_Store;

ByteSpan orb_write(Arena *arena, Orb *orb);
// Variable-sized data points into source; source must outlive the returned Orb.
Orb *orb_read(Arena *arena, ByteSpan source);
// Game data is borrowed and must outlive the returned Orb.
Orb *orb_from_game(Orb_Store *store, Orb_Game game);
// Replaces the store's current contents; failure leaves it empty. The store owns the loaded file data.
Orb *orb_from_file(Orb_Store *store, Str path);
Hash256 orb_game_hash(Orb_Game game);

void orb_store_init(Orb_Store *store);
void orb_store_destroy(Orb_Store *store);

#endif
