#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"
#include "mappers/mapper.h"
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

static b32 nes_mapper_supported(u32 mapper)
{
	if (mapper >= ArrayCount(nes_mapper_classes)) return false;
	if (!nes_mapper_classes[mapper].reset)        return false;
	if (!nes_mapper_classes[mapper].cpu_bus)      return false;
	if (!nes_mapper_classes[mapper].ppu_bus)      return false;
	return true;
}

static void nes_emulator_activate_mapper(NES_Emulator *emulator)
{
	Assert(nes_mapper_supported(emulator->core.mapper));
	emulator->mapper = nes_mapper_classes[emulator->core.mapper];
}

static b32 nes_cartridge_supported(NES_CartridgeDesc cart)
{
	if (cart.prg_rom.data == 0)                            return false;
	if (cart.prg_rom.size == 0)                            return false;
	if (cart.prg_rom.size > NES_MAX_PRG_ROM_SIZE)          return false;
	if (cart.prg_rom.size % KiB(16))                       return false;
	if (cart.chr_rom.size > NES_MAX_CHR_ROM_SIZE)          return false;
	if (cart.chr_rom.size % KiB(8))                        return false;
	if (cart.chr_rom.size && !cart.chr_rom.data)           return false;
	if (cart.mapper >= ArrayCount(nes_mapper_classes))     return false;
	if (cart.mapper == 0 && cart.prg_rom.size > KiB(32))    return false;
	return nes_mapper_supported(cart.mapper);
}

static b32 nes_state_valid(const NES_State *state)
{
	if (state->prg_rom_size == 0)                                return false;
	if (state->prg_rom_size > NES_MAX_PRG_ROM_SIZE)              return false;
	if (state->prg_rom_size % KiB(16))                           return false;
	if (state->chr_rom_size > NES_MAX_CHR_ROM_SIZE)              return false;
	if (state->chr_rom_size % KiB(8))                            return false;
	if (state->num_prg_banks != state->prg_rom_size / KiB(16))   return false;
	if (state->num_chr_banks != state->chr_rom_size / KiB(8))    return false;
	if (state->vmirror != 0 && state->vmirror != 1)              return false;
	if (state->ppu.xtick >= 341)                                 return false;
	if (state->ppu.ytick >= 262)                                 return false;
	if (state->ppu.t > 0x7FFF)                                   return false;
	if (state->ppu.v > 0x7FFF)                                   return false;
	if (state->ppu.x >= 8)                                       return false;
	if (state->ppu.w > 1)                                        return false;
	if (state->ppu.nsprs > NES_PPU_MAX_SPRITES_PER_SCANLINE)     return false;
	if (state->apu.mode > 1)                                     return false;
	if (state->apu.reset_mode > 1)                               return false;
	if (state->apu.reset_delay > 4)                              return false;
	if (state->apu.step_index >= (state->apu.mode ? 5 : 4))      return false;
	if (state->apu.triangle.wave_phase >= 32)                    return false;
	if (!nes_mapper_supported(state->mapper))                    return false;
	if (state->mapper == 0 && state->num_prg_banks > 2) return false;
	if (state->mapper == 9 && state->values[5] != 0xFD && state->values[5] != 0xFE) return false;
	if (state->mapper == 9 && state->values[6] != 0xFD && state->values[6] != 0xFE) return false;
	return true;
}

NES_Emulator *nes_emulator_create(Arena *arena)
{
	return arena_push_zero(arena, sizeof(NES_Emulator));
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
	return core->scheduler_clock;
}

void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input)
{
	if (player < ArrayCount(core->core.input_state.inputs))
	core->core.input_state.inputs[player] = (u8)input;
}

// TODO(RJ) REMOVE
NES_CPUState nes_emulator_cpu_state(const NES_Emulator *core)
{
	return core->core.cpu;
}

// TODO(RJ) REMOVE
NES_PPUState nes_emulator_ppu_state(const NES_Emulator *core)
{
	return core->core.ppu;
}

// TODO(RJ) REMOVE
NES_APUState nes_emulator_apu_state(const NES_Emulator *core)
{
	return core->core.apu;
}

// TODO(RJ) REMOVE
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

// TODO(RJ) REMOVE, this can be done by the adapter directly
//	Pattern tiles are made up of two faces, each is 8 bytes.
//	To create a palette index you join the two faces.
static NES_PatternTile nes_emulator_pattern_tile(NES_Emulator *core, u32 index)
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

// TODO(RJ) REMOVE, this can be done by the adapter directly
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

ByteSpan nes_emulator_save_state(NES_Emulator *core, Arena *arena)
{
	u8 *start = arena_top(arena);
	arena_ensure_committed(arena, arena->reserved_size - arena->position);
	u64 size = serialize_write_record(byte_span(start, arena->reserved_size - arena->position), nes_state_record_map(), NES_RECORD_EMULATOR, core);
	arena->position += size;
	return byte_span(start, size);
}

b32 nes_emulator_load_state(NES_Emulator *emulator, ByteSpan state_wire)
{
	NES_Emulator *dummy = calloc(1, sizeof(* dummy));
	if (!dummy) return false;
	b32 success = serialize_read_record(state_wire, nes_state_record_map(), NES_RECORD_EMULATOR, dummy);
	if (!success) goto cleanup;
	if (!nes_state_valid(& dummy->core))
	{
		success = false;
		goto cleanup;
	}
	memory_copy(emulator, dummy, sizeof(* dummy));
	nes_emulator_activate_mapper(emulator);
cleanup:
	free(dummy);
	return success;
}

b32 nes_emulator_load_cartridge(NES_Emulator *emulator, NES_CartridgeDesc cart)
{
	if (!nes_cartridge_supported(cart)) return false;
	memory_zero(emulator, sizeof(* emulator));
	memory_copy(emulator->core.prg_rom, cart.prg_rom.data, cart.prg_rom.size);
	memory_copy(emulator->core.chr_rom, cart.chr_rom.data, cart.chr_rom.size);
	emulator->core.num_prg_banks = cart.prg_rom.size / KiB(16);
	emulator->core.num_chr_banks = cart.chr_rom.size / KiB(8);
	emulator->core.prg_rom_size = cart.prg_rom.size;
	emulator->core.chr_rom_size = cart.chr_rom.size;
	emulator->core.mapper = cart.mapper;
	emulator->core.vmirror = cart.vertical_mirroring;
	nes_emulator_activate_mapper(emulator);
	emulator->mapper.reset(emulator);
	nes_ppu_reset(&emulator->core.ppu);
	nes_apu_reset(&emulator->core.apu);
	nes_cpu_reset(emulator);
	return true;
}

static inline u32 cpu_step(NES_Emulator *emulator)
{
	NES_CPUState *cpu = & emulator->core.cpu;

	// """
	// If the CPU's /IRQ input is 0 at the end of an instruction, then the CPU pushes the program counter
	// and the processor status register, sets the I flag to ignore further IRQs, and the Program Counter
	// takes the value read at $fffe and $ffff.
	// """
	b32 irq_line = emulator->core.apu.irq_pending;
	if (irq_line && (~ cpu->P & cpu_status_mask(CPU_STAT_I))) {
		return nes_cpu_irq(emulator);
	}
	//
	// Note, this is introspection stuff:
	// Has to be done here because the debugger doesn't have fine grain control over the CPU's execution
	//
	NES_BusAccess access = nes_cpu_bus_peek_mapped(emulator, cpu->PC);
	u64 trace_index = emulator->scheduler_trace_index;
	emulator->scheduler_trace[trace_index & NES_SCHEDULER_TRACE_CAPACITY_MASK] = nes_scheduler_trace_pack((NES_SchedulerBoundary) {
		.scheduler_clock = emulator->scheduler_clock,
		.cpu_address = cpu->PC,
		.cpu_mapped = access.mapped,
		.cpu_byte = access.value,
	});
	emulator->scheduler_trace_index = trace_index + 1;
	return nes_cpu_step(emulator);
}

typedef struct
{
	u32 cpu_cycles;
	u32 ppu_events;
}
NES_InstructionStep;

typedef struct
{
	f32 *samples;
	u64 sample_count;
	u64 sample_capacity;
}
NES_AudioOutput;

static inline b32 nes_sample_phase_advance(u64 *sample_phase, u64 sample_rate)
{
	*sample_phase += sample_rate;
	if (*sample_phase < NES_CPU_HZ) return false;
	*sample_phase -= NES_CPU_HZ;
	return true;
}

static inline void nes_audio_output_sample(NES_Emulator *emulator, NES_AudioOutput *output)
{
	if (output->samples)
	{
		Assert(output->sample_count < output->sample_capacity);
		output->samples[output->sample_count] = nes_apu_dac(&emulator->core.apu);
	}
	output->sample_count ++;
}

static NES_InstructionStep nes_emulator_step_internal(NES_Emulator *emulator, NES_AudioOutput *output)
{
	u32 ppu_events = 0;
	u32 cpu_cycles = cpu_step(emulator);
	for (u32 cycle = 0; cycle < cpu_cycles; ++cycle)
	{
		for (u32 ppu_cycle = 0; ppu_cycle < 3; ++ppu_cycle) {
			u32 events = nes_ppu_step(emulator);
			ppu_events |= events;
			if (events & NES_PPU_EVENT_NMI) nes_cpu_nmi(emulator);
		}

		nes_apu_clock_cpu_cycle(&emulator->core.apu);
		b32 emulator_sample = nes_sample_phase_advance(&emulator->sample_phase, nes_sample_rate(emulator));
		if (output && emulator_sample) nes_audio_output_sample(emulator, output);
	}
	prof_add_metric(PROF_METRIC_CPU_CYCLES, cpu_cycles);
	emulator->scheduler_clock ++;
	return (NES_InstructionStep) { .cpu_cycles = cpu_cycles, .ppu_events = ppu_events };
}

u32 nes_emulator_step(NES_Emulator *emulator)
{
	return nes_emulator_step_internal(emulator, 0).cpu_cycles;
}

NES_RunFrameResult nes_emulator_run_frame(NES_Emulator *emulator, f32 *sample_buffer, u64 sample_capacity)
{
	Assert(sample_buffer || !sample_capacity);
	NES_AudioOutput output = {
		.samples = sample_buffer,
		.sample_capacity = sample_capacity,
	};
	b32 frame_event = false;
	u64 steps = 0;
	while (!frame_event)
	{
		NES_InstructionStep step = nes_emulator_step_internal(emulator, &output);
		frame_event = !!(step.ppu_events & NES_PPU_EVENT_FRAME);
		steps ++;
	}

	NES_RunFrameResult result = {
		.steps = steps,
		.samples = output.sample_count,
	};
	return result;
}
