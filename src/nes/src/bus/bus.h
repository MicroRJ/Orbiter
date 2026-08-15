#ifndef NES_INTERNAL_BUS_H
#define NES_INTERNAL_BUS_H

#include "nes/emulator.h"

NES_BusResult nes_cpu_bus_peek_mapped(NES_Emulator *core, u16 address);

#endif
