#ifndef NES_EMULATOR_H
#define NES_EMULATOR_H

#include "base.h"
#include "nes/cartridge.h"

enum
{
	NES_PPU_MAX_SPRITES_PER_SCANLINE  = 8,
	NES_APU_MAX_PULSE_TIMER_VALUE     = 1 << 11,
	NES_MAX_CHR_ROM_SIZE              = MB(1)  ,
	NES_MAX_PRG_ROM_SIZE              = MB(1)  ,
	NES_MAX_CHR_RAM_SIZE              = MB(1)  ,
	NES_MAX_PRG_RAM_SIZE              = MB(1)  ,
	NES_VRAM_SIZE                     = 0x2000 ,
	NES_WRAM_SIZE                     = 0x2000 ,
	NES_CPU_ADDRESS_SPACE             = 0x10000,
	NES_PPU_ADDRESS_SPACE             = 0x4000 ,
	NES_CPU_HZ                        = 1789773,
	NES_VIDEO_WIDTH                   = 256,
	NES_VIDEO_HEIGHT                  = 240,
	NES_PPU_FRAME_CYCLES              = 262 * 341,

	NES_SCHEDULER_TRACE_CAPACITY_POW2 = 1 << 16,
	NES_SCHEDULER_TRACE_CAPACITY_MASK = NES_SCHEDULER_TRACE_CAPACITY_POW2 - 1,
};

typedef u8 NES_Input;
enum
{
	NES_INPUT_A      = 1,
	NES_INPUT_B      = 2,
	NES_INPUT_SELECT = 4,
	NES_INPUT_START  = 8,
	NES_INPUT_UP     = 16,
	NES_INPUT_DOWN   = 32,
	NES_INPUT_LEFT   = 64,
	NES_INPUT_RIGHT  = 128,
};

typedef struct
{
	u8  A;
	u8  X;
	u8  Y;
	u8  S;
	u8  P;
	u16 PC;
}
NES_CPUState;

// NOTE(RJ) this actually has to match OAM layout
typedef struct
{
	u8 ypos;
	u8 index;
	u8 attrs;
	u8 xpos;
}
NES_PPUSprite;

enum
{
	NES_PPU_EVENT_NONE  = 0,
	NES_PPU_EVENT_NMI   = 1 << 0,
	NES_PPU_EVENT_FRAME = 1 << 1,
};

typedef struct
{
	u16           xtick;
	u16           ytick;
	u16           t;
	u16           v;
	u8            x;
	u8            w;
	u8            tile_id;
	u8            tile_hi;
	u8            tile_lo;
	u8            atr_b;
	u8            atr_l0;
	u8            atr_l1;
	u16           chr_r0;
	u16           chr_r1;
	u8            atr_r0;
	u8            atr_r1;
	u8            spr0_enable;
	u8            spr0_2cycle_delay;
	u8            PPUCTRL;
	u8            PPUMASK;
	u8            PPUSTATUS;
	u8            OAMADDR;
	u8            data_read_buf;
	u8            nsprs;
	NES_PPUSprite sprs[NES_PPU_MAX_SPRITES_PER_SCANLINE];
	union
	{
		NES_PPUSprite OAM[64];
		u8           _oam[256];
	};
	u8 _pram[32];
}
NES_PPUState;

typedef struct
{
	u8 counter;
	u8 period;
}
NES_APUDivider;

typedef struct
{
	NES_APUDivider divider;
	u8 reload_divider;
	u8 shift;
	u8 enable;
	u8 negate;
}
NES_APUSweep;

typedef struct
{
	NES_APUDivider divider;
	u8             counter;
	u8             reload_divider;
}
NES_APUEnvelope;

typedef struct
{
	u8               enable;
	u8               infinite_play;
	u8               length_counter;
	u8               volume;
	u8               use_constant_volume;
	NES_APUSweep     sweep;
	NES_APUEnvelope  env;
	u8               duty_mask;
	u8               phase;
	u16              timer;
	u16              timer_period;
}
NES_APU_Pulse;

typedef struct
{
	u8  enable;

	u8  length_counter;
	u8  length_counter_halt;

	u8  linear_counter;
	u8  linear_counter_reload;
	u8  linear_counter_reload_value;

	u8  wave_phase;
	u16 wave_period;
	u16 wave_timer;
}
NES_APU_Triangle;

typedef struct
{
	u8              irq_pending;
	u8              irq_inhibit;
	u8              reset_delay;
	u8              reset_mode;
	u8              mode;
	u8              step_index;
	u16             cpu_cycle_counter;
	NES_APU_Pulse    pulse[2];
	NES_APU_Triangle triangle;
}
NES_APUState;

typedef struct { u32 number; } NES_MapperId;
typedef struct { u8 inputs[2]; } NES_InputState;

typedef struct NES_State
{
	// IDK PROB REMOVE FROM HERE!?
	NES_MapperId       mapper_number;
	u32 num_chr_banks, num_prg_banks;
	u32 prg_rom_size,  chr_rom_size;
	u32 prg_bank_size, chr_bank_size;
	b32                vmirror;
	// LIVE STATE
	u8             values[32];
	NES_InputState input_state;
	u32            cpu_stall_cycles;
	NES_CPUState   cpu;
	NES_PPUState   ppu;
	NES_APUState   apu;
	u8             controllers[2];
	u8             _wram[NES_WRAM_SIZE];
	u8             _vram[NES_VRAM_SIZE];
	u8             chr_ram[NES_MAX_CHR_RAM_SIZE];
	u8             prg_ram[NES_MAX_PRG_RAM_SIZE];
	u8             chr_rom[NES_MAX_CHR_ROM_SIZE];
	u8             prg_rom[NES_MAX_PRG_ROM_SIZE];
}
NES_State;

typedef struct NES_Emulator NES_Emulator;

typedef struct
{
	const u8 *pixels;
	u32       width;
	u32       height;
	u32       stride;
}
NES_VideoFrame;

typedef enum
{
	NES_DEVICE_NONE = 0,
	NES_DEVICE_CPU,
	NES_DEVICE_PPU,
	NES_DEVICE_OAM,
	NES_DEVICE_PRAM,
	NES_DEVICE_VRAM,
	NES_DEVICE_WRAM,
	NES_DEVICE_CHR_ROM,
	NES_DEVICE_CHR_RAM,
	NES_DEVICE_PRG_ROM,
	NES_DEVICE_PRG_RAM,
	NES_DEVICE_COUNT,
	NES_DEVICE_ENUM_FORCE_U32 = MAX_VALUE_U32,
}
NES_DeviceId;

typedef struct
{
	NES_DeviceId device;
	union
	{
		// TODO(RJ) REMOVE ALIASES
		u32 offset;
		u32 address;
		u32 addr;
	};
}
NES_MapAddr;

static inline NES_MapAddr nes_map_addr(NES_DeviceId device, u32 address)
{
	return (NES_MapAddr) { device, address };
}

typedef struct { u8 pixels[8][8]; } NES_PatternTile;

enum
{
	NES_PATTERN_TILE_SIZE = 8,
	NES_PATTERN_TABLE_TILE_COUNT = 256,
	NES_PATTERN_TILE_COUNT = NES_PATTERN_TABLE_TILE_COUNT * 2,
	NES_PALETTE_RAM_SIZE = 32,
};

typedef struct
{
	NES_PatternTile tiles[NES_PATTERN_TILE_COUNT];
	NES_MapAddr mappings[NES_PATTERN_TILE_COUNT];
	u8 palette[NES_PALETTE_RAM_SIZE];
}
NES_CHRMap;


NES_Emulator *nes_emulator_create(Arena *arena);

b32 nes_emulator_has_cartridge(const NES_Emulator *core);
u32 nes_emulator_prg_rom_size(const NES_Emulator *core);
// Copies the descriptor's borrowed PRG and CHR data into emulator-owned storage.
b32 nes_emulator_load_cartridge(NES_Emulator *core, NES_CartridgeDesc cartridge);
u64 nes_emulator_scheduler_clock(const NES_Emulator *core);
u32 nes_emulator_step(NES_Emulator *core);
// Runs whole instructions until at least minimum_samples have been captured at sample_rate.
// The caller owns sample_phase and must preserve it between calls. The returned count may be
// larger than requested, so capacity must include overshoot space.
u64 nes_emulator_run_samples(NES_Emulator *core, u32 sample_rate, u32 *sample_phase, u64 minimum_samples, f32 *samples, u64 capacity);
void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input);

// These return owned copies. Callers never receive aliases to mutable device state.
NES_CPUState nes_emulator_cpu_state(const NES_Emulator *core);
NES_PPUState nes_emulator_ppu_state(const NES_Emulator *core);
NES_APUState nes_emulator_apu_state(const NES_Emulator *core);
NES_VideoFrame nes_emulator_video_frame(const NES_Emulator *core);

u8 nes_emulator_cpu_peek(NES_Emulator *core, u16 address);
u16 nes_emulator_cpu_peek_word(NES_Emulator *core, u16 address);
NES_MapAddr nes_emulator_cpu_map(NES_Emulator *core, u16 address);
void nes_emulator_cpu_write(NES_Emulator *core, u16 address, u8 value);
void nes_emulator_capture_chr_map(NES_Emulator *core, NES_CHRMap *map);
ByteSpan nes_emulator_save_state(NES_Emulator *core, Arena *arena);
b32 nes_emulator_load_state(NES_Emulator *core, ByteSpan state);

typedef struct
{
	u64         scheduler_clock;
	u16         cpu_address;
	NES_MapAddr cpu_mapped;
	u8          cpu_byte;
}
NES_SchedulerBoundary;

typedef struct
{
	u64 bits;
}
NES_SchedulerTraceEntry;

STATIC_ASSERT(sizeof(NES_SchedulerTraceEntry) == 8);

enum
{
	NES_SCHEDULER_TRACE_CLOCK_SHIFT         = 0,
	NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT   = 16,
	NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT      = 32,
	NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT = 40,
	NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT = 44,
	NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK  = 0xF,
	NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK  = (1 << 20) - 1,
};

STATIC_ASSERT(NES_DEVICE_COUNT - 1 <= NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK);
STATIC_ASSERT(NES_MAX_PRG_ROM_SIZE - 1 <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);
STATIC_ASSERT(NES_MAX_PRG_RAM_SIZE - 1 <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);
STATIC_ASSERT(sizeof(NES_SchedulerTraceEntry) * NES_SCHEDULER_TRACE_CAPACITY_POW2 == KiB(512));

static __forceinline NES_SchedulerTraceEntry nes_scheduler_trace_pack(NES_SchedulerBoundary boundary)
{
	Assert((u32)boundary.cpu_mapped.device <= NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK);
	Assert(boundary.cpu_mapped.offset <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);
	return (NES_SchedulerTraceEntry) {
		.bits =
			((boundary.scheduler_clock & MAX_VALUE_U16) << NES_SCHEDULER_TRACE_CLOCK_SHIFT) |
			((u64)boundary.cpu_address << NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT) |
			((u64)boundary.cpu_byte << NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT) |
			((u64)boundary.cpu_mapped.device << NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT) |
			((u64)boundary.cpu_mapped.offset << NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT),
	};
}

typedef struct
{
	const NES_SchedulerTraceEntry *trace;
	u64                            index;
	u64                            scheduler_clock;
}
NES_SchedulerTraceView;

typedef struct
{
	const NES_SchedulerTraceEntry *entries;
	u32 count;
}
NES_SchedulerTraceSpan;

typedef struct
{
	NES_SchedulerTraceSpan spans[2];
	u64 dropped;
}
NES_SchedulerTraceSpans;

static inline u64 nes_scheduler_trace_first_since(NES_SchedulerTraceView view, u64 since)
{
	Assert(since <= view.index);
	u64 oldest = view.index > NES_SCHEDULER_TRACE_CAPACITY_POW2 ? view.index - NES_SCHEDULER_TRACE_CAPACITY_POW2 : 0;
	return Max(since, oldest);
}

static inline u64 nes_scheduler_trace_dropped_since(NES_SchedulerTraceView view, u64 since)
{
	return nes_scheduler_trace_first_since(view, since) - since;
}

static inline NES_SchedulerTraceSpans nes_scheduler_trace_spans_since(NES_SchedulerTraceView view, u64 since)
{
	u64 first = nes_scheduler_trace_first_since(view, since);
	u64 count = view.index - first;
	Assert(count <= NES_SCHEDULER_TRACE_CAPACITY_POW2);
	u32 first_slot = (u32)first & NES_SCHEDULER_TRACE_CAPACITY_MASK;
	u32 first_count = (u32)Min(count, NES_SCHEDULER_TRACE_CAPACITY_POW2 - first_slot);
	return (NES_SchedulerTraceSpans) {
		.spans = {
			{ .entries = view.trace + first_slot, .count = first_count },
			{ .entries = view.trace, .count = (u32)count - first_count },
		},
		.dropped = first - since,
	};
}

// NOTE(RJ) For compactness, each entry stores only the low 16 bits of the scheduler clock,
// so the clock is only reconstructible if the reader's clock is less than 16 bits worth of
// range away.
static __forceinline b32 nes_scheduler_trace_clock_reconstructable_since(NES_SchedulerTraceView view, u64 scheduler_clock)
{
	return scheduler_clock <= view.scheduler_clock && view.scheduler_clock - scheduler_clock <= MAX_VALUE_U16;
}

static __forceinline const NES_SchedulerTraceEntry *nes_scheduler_trace_entry_at(NES_SchedulerTraceView view, u64 index)
{
	u64 oldest = view.index > NES_SCHEDULER_TRACE_CAPACITY_POW2 ? view.index - NES_SCHEDULER_TRACE_CAPACITY_POW2 : 0;
	Assert(index >= oldest && index < view.index);
	return &view.trace[index & NES_SCHEDULER_TRACE_CAPACITY_MASK];
}

static __forceinline NES_SchedulerBoundary nes_scheduler_trace_decode(NES_SchedulerTraceView view, NES_SchedulerTraceEntry entry)
{
	u64 scheduler_clock = (view.scheduler_clock & ~(u64)MAX_VALUE_U16) | (u16)(entry.bits >> NES_SCHEDULER_TRACE_CLOCK_SHIFT);
	if (scheduler_clock > view.scheduler_clock)
	{
		Assert(scheduler_clock >= (u64)MAX_VALUE_U16 + 1);
		scheduler_clock -= (u64)MAX_VALUE_U16 + 1;
	}
	return (NES_SchedulerBoundary) {
		.scheduler_clock = scheduler_clock,
		.cpu_address = (u16)(entry.bits >> NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT),
		.cpu_mapped = {
			.device = (NES_DeviceId)((entry.bits >> NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT) & NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK),
			.offset = (u32)((entry.bits >> NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT) & NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK),
		},
		.cpu_byte = (u8)(entry.bits >> NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT),
	};
}

static __forceinline NES_SchedulerBoundary nes_scheduler_trace_at(NES_SchedulerTraceView view, u64 index)
{
	return nes_scheduler_trace_decode(view, *nes_scheduler_trace_entry_at(view, index));
}

NES_SchedulerTraceView nes_emulator_scheduler_trace(const NES_Emulator *core);

#endif
