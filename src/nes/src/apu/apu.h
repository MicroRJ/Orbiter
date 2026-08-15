
#ifndef NES_INTERNAL_APU_H
#define NES_INTERNAL_APU_H

#include "nes/emulator.h"
#include "../bus/bus.h"

enum
{
	// the timer is 11 bits
	APU_MAX_PULSE_TIMER_VALUE = 1 << 11,
};

void nes_apu_power_on(NES_APUState *apu);
void nes_apu_reset(NES_APUState *apu);
NES_BusResult nes_apu_register_access(NES_Emulator *core, NES_BusMode mode, u32 address, u8 value);
void nes_apu_clock_cpu_cycle(NES_APUState *apu);
f32 nes_apu_dac(const NES_APUState *apu);

#endif
