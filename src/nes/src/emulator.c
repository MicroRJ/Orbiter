#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"
#include "mappers/mapper.h"
#include "runtime/scheduler.h"
#include "nes/state_meta.h"
#include <stdlib.h>

static const NES_MapperClass nes_mapper_classes[] =
{
	{ "NROM",     nrom_reset,  nrom_cpu,  nrom_ppu },
	{ "MMC1",     mmc1_reset,  mmc1_cpu,  mmc1_ppu },
	{ "UxROM",   uxrom_reset, uxrom_cpu, uxrom_ppu },
	{ "UNKNOWN",           0,         0,         0 },
	{ "UNKNOWN",           0,         0,         0 },
	{ "UNKNOWN",           0,         0,         0 },
	{ "UNKNOWN",           0,         0,         0 },
	{ "UNKNOWN",           0,         0,         0 },
	{ "UNKNOWN",           0,         0,         0 },
	{ "MMC2",     mmc2_reset,  mmc2_cpu,  mmc2_ppu },
};


void nes_mapper_set_value(NES_Emulator *core, u32 index, u8 value)
{
	Assert(index < ArrayCount(core->core.values));
	core->core.values[index] = value;
}

static void nes_zero_machine(NES_Emulator *core)
{
	memory_zero(&core->core, sizeof(core->core));
	memory_zero(core->video, sizeof(core->video));
}

static void nes_reset_audio(NES_Emulator *core)
{
	core->core.audio_sample_phase = 0;
}

static void nes_zero_runtime(NES_Emulator *core)
{
	core->scheduler_clock = 0;
	memory_zero(&core->core.values, sizeof(core->core.values));
	memory_zero(&core->core.input_state, sizeof(core->core.input_state));
	memory_zero(&core->core.cpu_stall_cycles, sizeof(core->core.cpu_stall_cycles));
	memory_zero(&core->core.controllers, sizeof(core->core.controllers));
	memory_zero(&core->core._wram, sizeof(core->core._wram));
	memory_zero(&core->core._vram, sizeof(core->core._vram));
	memory_zero(&core->core.chr_ram, sizeof(core->core.chr_ram));
	memory_zero(&core->core.prg_ram, sizeof(core->core.prg_ram));
	nes_reset_audio(core);
}

static void nes_emulator_activate_mapper(NES_Emulator *core)
{
	u32 number = core->core.mapper_number.number;
	Assert(number < ArrayCount(nes_mapper_classes));
	Assert(nes_mapper_classes[number].reset);
	Assert(nes_mapper_classes[number].cpu_bus);
	Assert(nes_mapper_classes[number].ppu_bus);
	core->mapper = nes_mapper_classes[number];
}

static b32 nes_saved_state_valid(const NES_Emulator *core)
{
	const NES_State *state = &core->core;
	if (!state->prg_rom_size ||
		state->prg_rom_size > NES_MAX_PRG_ROM_SIZE ||
		state->prg_rom_size % KiB(16) ||
		state->chr_rom_size > NES_MAX_CHR_ROM_SIZE ||
		state->chr_rom_size % KiB(8) ||
		state->num_prg_banks != state->prg_rom_size / KiB(16) ||
		state->num_chr_banks != state->chr_rom_size / KiB(8) ||
		(state->vmirror != 0 && state->vmirror != 1) ||
		state->ppu.xtick >= 341 ||
		state->ppu.ytick >= 262 ||
		state->ppu.t > 0x7FFF ||
		state->ppu.v > 0x7FFF ||
		state->ppu.x >= 8 ||
		state->ppu.w > 1 ||
		state->ppu.nsprs > NES_PPU_MAX_SPRITES_PER_SCANLINE ||
		state->apu.mode > 1 ||
		state->apu.reset_mode > 1 ||
		state->apu.reset_delay > 4 ||
		state->apu.step_index >= (state->apu.mode ? 5 : 4) ||
		state->apu.triangle.wave_phase >= 32 ||
		state->audio_sample_phase >= NES_CPU_HZ)
	{
		return false;
	}
	if (state->mapper_number.number == 0 &&
		(state->num_prg_banks < 1 || state->num_prg_banks > 2)) {
		return false;
	}
	if (state->mapper_number.number == 9)
	{
		if ((state->values[5] != 0xFD && state->values[5] != 0xFE) ||
			(state->values[6] != 0xFD && state->values[6] != 0xFE)) {
			return false;
		}
	}
	return true;
}

NES_Emulator *nes_emulator_create(Arena *arena, NES_EmulatorDesc desc)
{
	NES_Emulator *core = arena_push_zero(arena, sizeof(*core));
	core->audio_sample_rate = desc.audio_sample_rate ? desc.audio_sample_rate : 48000;
	core->instruction_trace_enabled = desc.enable_instruction_trace;
	return core;
}

b32 nes_emulator_has_cartridge(const NES_Emulator *core)
{
	return core->mapper.reset != 0;
}

u32 nes_emulator_prg_rom_size(const NES_Emulator *core)
{
	return core->core.prg_rom_size;
}

u64 nes_emulator_scheduler_clock(const NES_Emulator *core)
{
	return core ? core->scheduler_clock : 0;
}

u32 nes_emulator_step(NES_Emulator *core)
{
	return nes_scheduler_step(core);
}

u64 nes_emulator_run_samples(NES_Emulator *core, u64 minimum_samples, f32 *samples, u64 capacity)
{
	return nes_scheduler_run_samples(core, minimum_samples, samples, capacity);
}

void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input)
{
	if (player < ArrayCount(core->core.input_state.inputs))
		core->core.input_state.inputs[player] = (u8)input;
}

NES_CPUState nes_emulator_cpu_state(const NES_Emulator *core)
{
	return core->core.cpu;
}

NES_PPUState nes_emulator_ppu_state(const NES_Emulator *core)
{
	return core->core.ppu;
}

NES_APUState nes_emulator_apu_state(const NES_Emulator *core)
{
	return core->core.apu;
}

NES_VideoFrame nes_emulator_video_frame(const NES_Emulator *core)
{
	return (NES_VideoFrame) {
		.pixels = &core->video[0][0],
		.width = NES_VIDEO_WIDTH,
		.height = NES_VIDEO_HEIGHT,
		.stride = NES_VIDEO_WIDTH,
	};
}

u8 nes_emulator_cpu_peek(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_peek(core, address);
}

u16 nes_emulator_cpu_peek_word(NES_Emulator *core, u16 address)
{
	u8 low = nes_cpu_bus_peek(core, address);
	u8 high = nes_cpu_bus_peek(core, (address + 1) & MAX_VALUE_U16);
	return low | (u16)high << 8;
}

NES_MapAddr nes_emulator_cpu_map(NES_Emulator *core, u16 address)
{
	return nes_cpu_bus_map(core, address);
}

//	Pattern tiles are made up of two faces, each is 8 bytes.
//	To create a palette index you join the two faces.
NES_PatternTile nes_emulator_pattern_tile(NES_Emulator *core, u32 index)
{
	Assert(index < NES_PATTERN_TILE_COUNT);
	NES_PatternTile tile = {};
	u32 address = index << 4;
	for (u32 y = 0; y < 8; ++y)
	{
		u32 lo = nes_ppu_bus_peek(core, address + y);
		u32 hi = nes_ppu_bus_peek(core, address + 8 + y);
		for (u32 x = 0; x < 8; ++x)
		{
			u32 palette_index = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
			tile.pixels[y][x] = (u8)palette_index;
		}
	}
	return tile;
}

void nes_emulator_capture_chr_map(NES_Emulator *core, NES_CHRMap *map)
{
	Assert(map);
	for (u32 index = 0; index < NES_PATTERN_TILE_COUNT; ++index)
	{
		map->tiles[index] = nes_emulator_pattern_tile(core, index);
		map->mappings[index] = nes_ppu_bus_map(core, (u16)(index << 4));
	}
	for (u32 index = 0; index < NES_PALETTE_RAM_SIZE; ++index) {
		map->palette[index] = nes_ppu_bus_peek(core, 0x3F00 + index);
	}
}

NES_SchedulerTraceView nes_emulator_scheduler_trace(const NES_Emulator *core)
{
	return (NES_SchedulerTraceView) { .trace = core->scheduler_trace, .index = core->scheduler_trace_index, .scheduler_clock = core->scheduler_clock };
}

void nes_emulator_cpu_write(NES_Emulator *core, u16 address, u8 value)
{
	nes_cpu_bus_write(core, address, value);
}

void nes_emulator_reset(NES_Emulator *core)
{
	nes_zero_runtime(core);
	memory_zero(core->video, sizeof(core->video));
	core->mapper.reset(core);
	nes_ppu_reset(&core->core.ppu);
	nes_apu_reset(&core->core.apu);
	nes_cpu_reset(core);
}

ByteSpan nes_emulator_save_state(NES_Emulator *core, Arena *arena)
{
	u8 *start = arena_top(arena);
	arena_ensure_committed(arena, arena->reserved_size - arena->position);
	u64 size = serialize_write_record(byte_span(start, arena->reserved_size - arena->position), nes_state_record_map(), NES_RECORD_EMULATOR, core);
	arena->position += size;
	return byte_span(start, size);
}

b32 nes_emulator_load_state(NES_Emulator *core, ByteSpan state)
{
	if (!core) return false;
	NES_Emulator *candidate = malloc(sizeof(*candidate));
	if (!candidate) return false;
	memory_zero(candidate, sizeof(*candidate));
	candidate->audio_sample_rate = core->audio_sample_rate;
	candidate->instruction_trace_enabled = core->instruction_trace_enabled;

	b32 success = serialize_read_record(state, nes_state_record_map(), NES_RECORD_EMULATOR, candidate);
	if (success) success = nes_saved_state_valid(candidate);

	if (success)
	{
		nes_emulator_activate_mapper(candidate);
		candidate->scheduler_clock = 0;
		*core = *candidate;
	}
	free(candidate);
	return success;
}

// Todo, this should be transactional!
b32 nes_emulator_load_cartridge(NES_Emulator *emulator, NES_CartridgeDesc cart)
{
	if (cart.prg_rom.data == 0)                            return false;
	if (cart.prg_rom.size == 0)                            return false;
	if (cart.prg_rom.size > NES_MAX_PRG_ROM_SIZE)          return false;
	if (cart.prg_rom.size % KiB(16))                       return false;
	if (cart.chr_rom.size > NES_MAX_CHR_ROM_SIZE)          return false;
	if (cart.chr_rom.size % KiB(8))                        return false;
	if (cart.chr_rom.size && !cart.chr_rom.data)           return false;
	if (cart.mapper >= ArrayCount(nes_mapper_classes))     return false;
	if (!nes_mapper_classes[cart.mapper].reset)            return false;

	nes_reset_audio(emulator);
	nes_zero_machine(emulator);
	memory_copy(emulator->core.prg_rom, cart.prg_rom.data, cart.prg_rom.size);
	memory_copy(emulator->core.chr_rom, cart.chr_rom.data, cart.chr_rom.size);
	emulator->core.num_prg_banks = cart.prg_rom.size / KiB(16);
	emulator->core.num_chr_banks = cart.chr_rom.size / KiB(8);
	emulator->core.prg_rom_size = cart.prg_rom.size;
	emulator->core.chr_rom_size = cart.chr_rom.size;
	emulator->core.mapper_number.number = cart.mapper;
	emulator->core.vmirror = cart.vertical_mirroring;
	nes_emulator_activate_mapper(emulator);
	nes_emulator_reset(emulator);
	return true;
}
