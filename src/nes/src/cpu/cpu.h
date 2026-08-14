#ifndef NES_INTERNAL_CPU_H
#define NES_INTERNAL_CPU_H

#include "nes/emulator.h"
#include "nes/isa.h"
#include "../bus/bus.h"

enum { RESET_VECTOR = 0xFFFC };

//
//		7  bit  0
//		---- ----
//		NV1B DIZC
//		|||| ||||
//		|||| |||+- Carry
//		|||| ||+-- Zero
//		|||| |+--- Interrupt Disable
//		|||| +---- Decimal
//		|||+------ (No CPU effect; see: the B flag)
//		||+------- (No CPU effect; always pushed as 1)
//		|+-------- Overflow
//		+--------- Negative
//

enum
{
	CPU_STAT_C = 0,
	CPU_STAT_Z = 1,
	CPU_STAT_I = 2,
	CPU_STAT_D = 3,
	CPU_STAT_B = 4,
	CPU_STAT_1 = 5,
	CPU_STAT_V = 6,
	CPU_STAT_N = 7,
};

void nes_cpu_nmi(NES_Emulator *core);
u32 nes_cpu_irq(NES_Emulator *core);
void nes_cpu_power_on(NES_Emulator *core);
void nes_cpu_reset(NES_Emulator *core);
u32 nes_cpu_step(NES_Emulator *core);


static inline u32 cpu_status_mask(u32 flag)
{
	return 1u << flag;
}

#endif
