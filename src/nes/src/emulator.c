#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"
#include "mappers/mapper.h"
#include "runtime/scheduler.h"
#include "nes/state_meta.h"

static const NES_MapperClass nes_mapper_classes[] =
{
	{ "NROM",    nrom_init,  nrom_reset,  nrom_cpu,  nrom_ppu },
	{ "MMC1",    mmc1_init,  mmc1_reset,  mmc1_cpu,  mmc1_ppu },
	{ "UxROM",  uxrom_init, uxrom_reset, uxrom_cpu, uxrom_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "UNKNOWN", none_init,  none_reset,  none_cpu,  none_ppu },
	{ "MMC2",    mmc2_init,  mmc2_reset,  mmc2_cpu,  mmc2_ppu },
};

static const NES_MapperClass nes_no_mapper =
{
	"NONE", none_init, none_reset, none_cpu, none_ppu,
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

NES_InstructionBoundary *nes_record_instruction_boundary(NES_Emulator *core, u16 cpu_address)
{
	if (core->instruction_boundary_count == NES_INSTRUCTION_BOUNDARY_CAPACITY)
	{
		core->instruction_boundary_dropped++;
		return 0;
	}
	NES_InstructionBoundary *boundary = &core->instruction_boundaries[core->instruction_boundary_count++];
	*boundary = (NES_InstructionBoundary) {
		.scheduler_clock = core->scheduler_clock,
		.cpu_address = cpu_address,
	};
	return boundary;
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

static b32 nes_instantiate_cartridge(NES_Emulator *core)
{
	core->mapper = nes_no_mapper;
	u32 mapper_number = core->core.mapper_number.number;
	if (mapper_number >= ArrayCount(nes_mapper_classes)) return false;

	NES_MapperClass mapper = nes_mapper_classes[mapper_number];
	NES_MapperInitParams params = { .num_prg_banks = core->core.num_prg_banks };
	b32 success = mapper.init(core, &params);
	if (success) core->mapper = mapper;
	return success;
}

NES_Emulator *nes_emulator_create(Arena *arena, NES_EmulatorDesc desc)
{
	NES_Emulator *core = arena_push_zero(arena, sizeof(*core));
	core->mapper = nes_no_mapper;
	core->instruction_trace_enabled = desc.enable_instruction_trace;
	core->instruction_boundaries_enabled = desc.enable_instruction_boundaries;
	core->audio_sample_rate = desc.audio_sample_rate ? desc.audio_sample_rate : 48000;
	return core;
}

b32 nes_emulator_has_cartridge(const NES_Emulator *core)
{
	return core && core->mapper.cpu_bus != none_cpu;
}

u32 nes_emulator_prg_rom_size(const NES_Emulator *core)
{
	return core ? core->core.prg_rom_size : 0;
}

b32 nes_emulator_load_cartridge(NES_Emulator *core, NES_CartridgeDesc cartridge)
{
	if (!cartridge.prg_rom.data || !cartridge.prg_rom.size ||
		cartridge.prg_rom.size > NES_MAX_PRG_ROM_SIZE ||
		cartridge.prg_rom.size % KiB(16) ||
		cartridge.chr_rom.size > NES_MAX_CHR_ROM_SIZE ||
		cartridge.chr_rom.size % KiB(8) ||
		(cartridge.chr_rom.size && !cartridge.chr_rom.data) ||
		cartridge.mapper >= ArrayCount(nes_mapper_classes))
	{
		return false;
	}

	nes_reset_audio(core);
	nes_zero_machine(core);
	memory_copy(core->core.prg_rom, cartridge.prg_rom.data, cartridge.prg_rom.size);
	memory_copy(core->core.chr_rom, cartridge.chr_rom.data, cartridge.chr_rom.size);
	core->core.num_prg_banks = cartridge.prg_rom.size / KiB(16);
	core->core.num_chr_banks = cartridge.chr_rom.size / KiB(8);
	core->core.prg_rom_size = cartridge.prg_rom.size;
	core->core.chr_rom_size = cartridge.chr_rom.size;
	core->core.mapper_number.number = cartridge.mapper;
	core->core.vmirror = cartridge.vertical_mirroring;
	if (!nes_instantiate_cartridge(core)) {
		return false;
	}
	nes_emulator_reset(core);
	return true;
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

u64 nes_emulator_scheduler_clock(const NES_Emulator *core)
{
	return core ? core->scheduler_clock : 0;
}

void nes_emulator_run(NES_Emulator *core, u64 ppu_cycles)
{
	core->instruction_trace_count = 0;
	core->instruction_trace_dropped = 0;
	core->instruction_boundary_count = 0;
	core->instruction_boundary_dropped = 0;
	nes_scheduler_run(core, ppu_cycles);
}

u32 nes_emulator_step(NES_Emulator *core)
{
	core->instruction_trace_count = 0;
	core->instruction_trace_dropped = 0;
	core->instruction_boundary_count = 0;
	core->instruction_boundary_dropped = 0;
	return nes_scheduler_step(core);
}

u64 nes_emulator_run_samples(NES_Emulator *core, u64 minimum_samples, f32 *samples, u64 capacity)
{
	core->instruction_trace_count = 0;
	core->instruction_trace_dropped = 0;
	core->instruction_boundary_count = 0;
	core->instruction_boundary_dropped = 0;
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

NES_InstructionTraceSpan nes_emulator_instruction_trace(const NES_Emulator *core)
{
	return (NES_InstructionTraceSpan) {
		.events = core->instruction_trace,
		.count = core->instruction_trace_count,
		.dropped = core->instruction_trace_dropped,
	};
}

NES_InstructionBoundarySpan nes_emulator_instruction_boundaries(const NES_Emulator *core)
{
	return (NES_InstructionBoundarySpan) {
		.items = core->instruction_boundaries,
		.count = core->instruction_boundary_count,
		.dropped = core->instruction_boundary_dropped,
	};
}

void nes_emulator_cpu_write(NES_Emulator *core, u16 address, u8 value)
{
	nes_cpu_bus_write(core, address, value);
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
	if (!serialize_read_record(state, nes_state_record_map(), NES_RECORD_EMULATOR, core))
	{
		LOG_ERROR("failed to load state: incompatible or invalid serialization format");
		return false;
	}
	b32 success = nes_instantiate_cartridge(core);
	if (success) {
		core->scheduler_clock = 0;
	}
	return success;
}
