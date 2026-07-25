
#ifndef NES_INTERNAL_SCHEDULER_H
#define NES_INTERNAL_SCHEDULER_H

#include "nes/emulator.h"

u32 nes_scheduler_step(NES_Emulator *core);
void nes_scheduler_run(NES_Emulator *core, u64 target_master_cycles);
u64 nes_scheduler_run_samples(NES_Emulator *core, u64 minimum_samples, f32 *samples, u64 capacity);

#endif
