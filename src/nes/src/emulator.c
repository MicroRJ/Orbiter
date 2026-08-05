#include "nes/emulator.h"
#include "emulator_internal.h"
#include "bus/bus.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"
#include "mappers/mapper.h"

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
	Assert(index < ArrayCount(core->values));
	core->values[index] = value;
}

static b32 nes_mapper_supported(u32 mapper)
{
	if (mapper >= ArrayCount(nes_mapper_classes)) return false;
	if (!nes_mapper_classes[mapper].reset)        return false;
	if (!nes_mapper_classes[mapper].cpu_bus)      return false;
	if (!nes_mapper_classes[mapper].ppu_bus)      return false;
	return true;
}

b32 nes_emulator_valid(const NES_Emulator *emulator)
{
	if (emulator->prg_rom_size == 0)                                      return false;
	if (emulator->prg_rom_size > NES_MAX_PRG_ROM_SIZE)                    return false;
	if (emulator->prg_rom_size % KiB(16))                                 return false;
	if (emulator->chr_rom_size > NES_MAX_CHR_ROM_SIZE)                    return false;
	if (emulator->chr_rom_size % KiB(8))                                  return false;
	if (emulator->num_prg_banks != emulator->prg_rom_size / KiB(16))      return false;
	if (emulator->num_chr_banks != emulator->chr_rom_size / KiB(8))       return false;
	if (emulator->vmirror != 0 && emulator->vmirror != 1)                 return false;
	if (emulator->ppu.xtick >= 341)                                       return false;
	if (emulator->ppu.ytick >= 262)                                       return false;
	if (emulator->ppu.t > 0x7FFF)                                         return false;
	if (emulator->ppu.v > 0x7FFF)                                         return false;
	if (emulator->ppu.x >= 8)                                             return false;
	if (emulator->ppu.w > 1)                                              return false;
	if (emulator->ppu.nsprs > NES_PPU_MAX_SPRITES_PER_SCANLINE)           return false;
	if (emulator->apu.mode > 1)                                           return false;
	if (emulator->apu.reset_mode > 1)                                     return false;
	if (emulator->apu.reset_delay > 4)                                    return false;
	if (emulator->apu.step_index >= (emulator->apu.mode ? 5 : 4))         return false;
	if (emulator->apu.triangle.wave_phase >= 32)                          return false;
	if (!nes_mapper_supported(emulator->mapper_number))                   return false;
	if (emulator->mapper_number == 0 && emulator->num_prg_banks > 2)      return false;
	if (emulator->mapper_number == 9 && emulator->values[5] != 0xFD && emulator->values[5] != 0xFE) return false;
	if (emulator->mapper_number == 9 && emulator->values[6] != 0xFD && emulator->values[6] != 0xFE) return false;
	return true;
}

// TODO(RJ) return a proper error code here!
b32 nes_supports_setup_params(NES_SetupParams params)
{
	if (params.has_trainer || params.four_screen)            return false;

	if (params.prg_rom.size == 0)                            return false;
	if (params.prg_rom.size > NES_MAX_PRG_ROM_SIZE)          return false;
	if (params.prg_rom.size % KiB(16))                       return false;

	if (params.chr_rom.size > NES_MAX_CHR_ROM_SIZE)          return false;
	if (params.chr_rom.size % KiB(8))                        return false;

	if (!nes_mapper_supported(params.mapper))                return false;
	if (params.mapper == 0 && params.prg_rom.size > KiB(32)) return false;
	if (params.mapper == 2 && params.chr_rom.size)           return false;
	if (params.mapper == 9 && (params.prg_rom.size < KiB(32) || !params.chr_rom.size)) return false;
	return true;
}

b32 nes_bootup_emulator(NES_Emulator *emulator)
{
	if (!nes_emulator_valid(emulator)) return false;
	emulator->mapper = nes_mapper_classes[emulator->mapper_number];
	return true;
}

b32 nes_setup_emulator(NES_Emulator *emulator, NES_SetupParams params)
{
	if (!nes_supports_setup_params(params)) return false;
	// TODO(RJ) only zero the live state
	memory_zero(emulator, sizeof(* emulator));

	memory_copy(emulator->prg_rom, params.prg_rom.data, params.prg_rom.size);
	memory_copy(emulator->chr_rom, params.chr_rom.data, params.chr_rom.size);
	emulator->mapper_number = params.mapper;
	emulator->prg_rom_size = params.prg_rom.size;
	emulator->chr_rom_size = params.chr_rom.size;
	emulator->vmirror = params.vmirror;
	// TODO(RJ) remove these two
	emulator->num_prg_banks = params.prg_rom.size / KiB(16);
	emulator->num_chr_banks = params.chr_rom.size / KiB(8);
	nes_bootup_emulator(emulator);
	Assert(emulator->mapper.reset(emulator));
	nes_ppu_reset(&emulator->ppu);
	nes_apu_reset(&emulator->apu);
	nes_cpu_reset(emulator);
	return true;
}

void nes_reset_emulator(NES_Emulator *emulator)
{
	Assert(nes_emulator_ready_to_run(emulator));
	emulator->cpu_stall_cycles = 0;
	Assert(emulator->mapper.reset(emulator));
	nes_ppu_reset(&emulator->ppu);
	nes_apu_reset(&emulator->apu);
	nes_cpu_reset(emulator);
}

b32 nes_emulator_ready_to_run(const NES_Emulator *core)
{
	return core->mapper.reset != 0;
}

u32 nes_emulator_prg_rom_size(const NES_Emulator *core)
{
	return core->prg_rom_size;
}

u64 nes_emulator_scheduler_clock(const NES_Emulator *core)
{
	return core->scheduler_clock;
}

// TODO(RJ) literally just pass this when running
void nes_emulator_set_input(NES_Emulator *core, u32 player, NES_Input input)
{
	if (player < ArrayCount(core->input_state.inputs))
	core->input_state.inputs[player] = (u8)input;
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

static inline u32 cpu_step(NES_Emulator *emulator, NES_TraceEntry *trace)
{
	NES_CPUState *cpu = & emulator->cpu;
	// One PC sample is emitted per scheduler step. An interrupt-only step may
	// sample a PC whose instruction does not execute; this is intentional because
	// trace indices correspond exactly to scheduler-step indices.
	if (trace)
	{
		NES_BusAccess access = nes_cpu_bus_peek_mapped(emulator, cpu->PC);
		*trace = (NES_TraceEntry) {
			.cpu_address = cpu->PC,
			.cpu_mapped = access.mapped,
			.cpu_byte = access.value,
		};
	}

	// """
	// If the CPU's /IRQ input is 0 at the end of an instruction, then the CPU pushes the program counter
	// and the processor status register, sets the I flag to ignore further IRQs, and the Program Counter
	// takes the value read at $fffe and $ffff.
	// """
	b32 irq_line = emulator->apu.irq_pending;
	if (irq_line && (~ cpu->P & cpu_status_mask(CPU_STAT_I))) {
		return nes_cpu_irq(emulator);
	}
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
		output->samples[output->sample_count] = nes_apu_dac(&emulator->apu);
	}
	output->sample_count ++;
}

static NES_InstructionStep nes_emulator_step_internal(NES_Emulator *emulator, NES_AudioOutput *output, NES_TraceEntry *trace)
{
	u32 ppu_events = 0;
	u32 cpu_cycles = cpu_step(emulator, trace);
	for (u32 cycle = 0; cycle < cpu_cycles; ++cycle)
	{
		for (u32 ppu_cycle = 0; ppu_cycle < 3; ++ppu_cycle) {
			u32 events = nes_ppu_step(emulator);
			ppu_events |= events;
			if (events & NES_PPU_EVENT_NMI) nes_cpu_nmi(emulator);
		}

		nes_apu_clock_cpu_cycle(&emulator->apu);
		b32 emulator_sample = nes_sample_phase_advance(&emulator->sample_phase, nes_sample_rate(emulator));
		if (output && emulator_sample) nes_audio_output_sample(emulator, output);
	}
	prof_add_metric(PROF_METRIC_CPU_CYCLES, cpu_cycles);
	emulator->scheduler_clock ++;
	return (NES_InstructionStep) { .cpu_cycles = cpu_cycles, .ppu_events = ppu_events };
}

u32 nes_emulator_step(NES_Emulator *emulator, NES_TraceEntry *trace)
{
	return nes_emulator_step_internal(emulator, 0, trace).cpu_cycles;
}

NES_RunFrameResult nes_emulator_run_frame(NES_Emulator *emulator, NES_RunParams params)
{
	Assert(params.samples || !params.sample_capacity);
	Assert(params.trace || !params.trace_capacity);
	NES_AudioOutput output = {
		.samples = params.samples,
		.sample_capacity = params.sample_capacity,
	};
	b32 frame_event = false;
	u64 steps = 0;
	while (!frame_event)
	{
		NES_TraceEntry *trace = 0;
		if (params.trace)
		{
			Assert(steps < params.trace_capacity);
			trace = params.trace + steps;
		}
		NES_InstructionStep step = nes_emulator_step_internal(emulator, &output, trace);
		frame_event = !!(step.ppu_events & NES_PPU_EVENT_FRAME);
		steps ++;
	}

	NES_RunFrameResult result = {
		.steps = steps,
		.samples = output.sample_count,
	};
	return result;
}
