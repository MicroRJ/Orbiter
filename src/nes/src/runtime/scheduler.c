
#include "scheduler.h"
#include "../emulator_internal.h"
#include "../cpu/cpu.h"
#include "../ppu/ppu.h"
#include "../apu/apu.h"

static inline void nes_scheduler_run_ppu(NES_Emulator *core) {
	u32 events = nes_ppu_step(core);
	if (events & NES_PPU_EVENT_NMI) {
		nes_cpu_nmi(core);
	}
}

// If the CPU's /IRQ input is 0 at the end of an instruction, then the CPU pushes the program counter
// and the processor status register, sets the I flag to ignore further IRQs, and the Program Counter
// takes the value read at $fffe and $ffff.
static inline u32 nes_scheduler_cpu_step(NES_Emulator *core)
{
	b32 irq_line = core->core.apu.irq_pending;
	if (irq_line && (~ core->core.cpu.P & cpu_status_mask(CPU_STAT_I))) {
		return nes_cpu_irq(core);
	}
	return nes_cpu_step(core);
}

static u32 nes_scheduler_step_internal(NES_Emulator *core, f32 *samples, u64 capacity, u64 *sample_count)
{
	u32 cpu_cycles = nes_scheduler_cpu_step(core);
	for (u32 cycle = 0; cycle < cpu_cycles; ++cycle)
	{
		for (u32 ppu_cycle = 0; ppu_cycle < 3; ++ppu_cycle) {
			nes_scheduler_run_ppu(core);
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
