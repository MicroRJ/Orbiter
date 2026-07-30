#ifndef NES_INTERNAL_MACHINE_H
#define NES_INTERNAL_MACHINE_H

#include "nes/emulator.h"
#include "mappers/mapper.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"

typedef struct
{
	u64 reads;
	u64 writes;
}
NES_BusMetrics;

struct NES_Emulator
{
	NES_State               core;
	NES_MapperClass         mapper;
	u64                     scheduler_clock;
	NES_BusMetrics          cpu_bus_metrics;
	NES_BusMetrics          ppu_bus_metrics;
	u64                     scheduler_trace_index;
	u8                      video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
	NES_SchedulerTraceEntry scheduler_trace[NES_SCHEDULER_TRACE_CAPACITY_POW2];
};

#endif
