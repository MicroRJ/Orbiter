
#include "scheduler.h"
#include "../emulator_internal.h"
#include "../cpu/cpu.h"
#include "../ppu/ppu.h"
#include "../apu/apu.h"

static inline u32 nes_scheduler_cpu_step(NES_Emulator *core)
{
	NES_CPUState *cpu = & core->core.cpu;

	// """
	// If the CPU's /IRQ input is 0 at the end of an instruction, then the CPU pushes the program counter
	// and the processor status register, sets the I flag to ignore further IRQs, and the Program Counter
	// takes the value read at $fffe and $ffff.
	// """
	b32 irq_line = core->core.apu.irq_pending;
	if (irq_line && (~ cpu->P & cpu_status_mask(CPU_STAT_I))) {
		return nes_cpu_irq(core);
	}
	//
	// Note, this is introspection stuff:
	// Has to be done here because the debugger doesn't have fine grain control over the CPU's execution
	//
	if (1) {
		NES_BusAccess access = nes_cpu_bus_peek_mapped(core, cpu->PC);
		u64 trace_index = core->scheduler_trace_index;
		core->scheduler_trace[trace_index & NES_SCHEDULER_TRACE_CAPACITY_MASK] = (NES_SchedulerBoundary) {
			.scheduler_clock = core->scheduler_clock,
			.cpu_address = cpu->PC,
			.cpu_mapped = access.mapped,
			.cpu_byte = access.value,
		};
		core->scheduler_trace_index = trace_index + 1;
	}
	return nes_cpu_step(core);
}

// Todo, we may want to average the samples ...
static u32 nes_scheduler_step_internal(NES_Emulator *core, f32 *samples, u64 capacity, u64 *sample_count)
{
	u32 cpu_cycles = nes_scheduler_cpu_step(core);
	for (u32 cycle = 0; cycle < cpu_cycles; ++cycle)
	{
		for (u32 ppu_cycle = 0; ppu_cycle < 3; ++ppu_cycle) {
			u32 events = nes_ppu_step(core);
			if (events & NES_PPU_EVENT_NMI) nes_cpu_nmi(core);
		}
		nes_apu_clock_cpu_cycle(&core->core.apu);
		core->core.audio_sample_phase += core->audio_sample_rate;
		while (core->core.audio_sample_phase >= NES_CPU_HZ)
		{
			core->core.audio_sample_phase -= NES_CPU_HZ;
			if (samples)
			{
				Assert(*sample_count < capacity);
				samples[(*sample_count)++] = nes_apu_dac(&core->core.apu);
			}
		}
	}
	prof_add_metric(PROF_METRIC_CPU_CYCLES, cpu_cycles);
	core->scheduler_clock ++;
	return cpu_cycles;
}

u32 nes_scheduler_step(NES_Emulator *core)
{
	return nes_scheduler_step_internal(core, 0, 0, 0);
}

void nes_scheduler_run(NES_Emulator *core, u64 target_master_cycles)
{
	i64 target_cpu_cycles = target_master_cycles / 3;

	while (target_cpu_cycles > 0)
	{
		u32 cpu_cycles = nes_scheduler_step(core);
		target_cpu_cycles -= cpu_cycles;
	}
}

u64 nes_scheduler_run_samples(NES_Emulator *core, u64 minimum_samples, f32 *samples, u64 capacity)
{
	Assert(samples || capacity == 0);
	Assert(minimum_samples <= capacity);
	u64 sample_count = 0;
	while (sample_count < minimum_samples)
	{
		nes_scheduler_step_internal(core, samples, capacity, &sample_count);
	}
	return sample_count;
}

// NES execution scheduler.
