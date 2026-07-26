#ifndef NES_INTERNAL_MACHINE_H
#define NES_INTERNAL_MACHINE_H

#include "nes/emulator.h"
#include "mappers/mapper.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "apu/apu.h"

struct NES_Emulator
{
	NES_State        core;
	u8               video[NES_VIDEO_HEIGHT][NES_VIDEO_WIDTH];
	NES_MapperClass  mapper;
	u32              audio_sample_rate;
	u64              scheduler_clock;
	b32              instruction_trace_enabled;
	b32              instruction_boundaries_enabled;
	u32              instruction_trace_count;
	u32              instruction_trace_dropped;
	NES_InstructionTrace instruction_trace[NES_INSTRUCTION_TRACE_CAPACITY];
	u32 instruction_boundary_count;
	u32 instruction_boundary_dropped;
	NES_InstructionBoundary instruction_boundaries[NES_INSTRUCTION_BOUNDARY_CAPACITY];
};

NES_InstructionBoundary *nes_record_instruction_boundary(NES_Emulator *core, u16 cpu_address);

#endif
