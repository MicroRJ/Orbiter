#ifndef FRONTEND_PROGRAM_H
#define FRONTEND_PROGRAM_H

#include "base.h"
#include "nes/emulator.h"

typedef struct Debugger Debugger;

enum
{
	PROGRAM_BYTE_UNKNOWN  = 0xFF,
	PROGRAM_BUCKET_SIZE   = KiB(4),
	PROGRAM_MAX_PRG_SIZE  = MB(1),
	PROGRAM_PRG_RAM_SIZE  = KiB(8),
	PROGRAM_MAX_SIZE      = PROGRAM_MAX_PRG_SIZE + PROGRAM_PRG_RAM_SIZE,
	PROGRAM_BUCKET_COUNT  = PROGRAM_MAX_SIZE / PROGRAM_BUCKET_SIZE,
};

typedef enum
{
	PROGRAM_NONE        = 0,
	PROGRAM_INSTRUCTION = 1,
}
ProgramTagsEnum;

typedef u8 ProgramTags;

typedef enum
{
	PROGRAM_INSTRUCTION_STATIC   = 1,
	PROGRAM_INSTRUCTION_EXECUTED = 2,
}
ProgramInstructionFlags;

typedef struct
{
	// Zero marks an instruction start, one or two point back to its start,
	// and PROGRAM_BYTE_UNKNOWN marks an unclassified PRG-ROM byte.
	u8  offset_to_start;
	u8  flags;
	u16 bridges;
	u16 next_bridges;
	u8  indent;
	u8  next_indent;
	u8  next_branch_seen;
	u8  value;
	u8  value_valid;
}
ProgramByte;

typedef struct
{
	u64 revision;
	u64 executed_instruction_count;
	u64 instruction_conflict_count;
	u64 executed_instruction_conflict_count;
	u64 discontinuous_instruction_count;
	u32 instruction_count;
	u32 byte_count;
	u32 prg_rom_byte_count;
	u32 prg_ram_byte_count;
	u32 instruction_bucket_count;
	u32 instruction_buckets[PROGRAM_BUCKET_COUNT];
	u32 refinement_cpu_cursor;
	u64 refinement_pass_count;
	ProgramByte bytes[PROGRAM_MAX_SIZE];
}
Program;

typedef enum
{
	PROGRAM_ROW_NONE,
	PROGRAM_ROW_INSTRUCTION,
	PROGRAM_ROW_GUESS,
	PROGRAM_ROW_ERROR,
}
ProgramRowStatus;

typedef struct
{
	NES_MapAddr map_addr;
	u16 cpu_address;
	i16 indent;
	u16 bridges;
	u16 type;
	u16 data;
	u16 advance;
	ProgramRowStatus status;
}
ProgramInstruction;

typedef struct
{
	ProgramInstruction *items;
	u32 count;
}
ProgramSlice;

ProgramTags program_tags(const Program *program, u32 prg_rom_offset);
u32 program_instruction_offset(const Program *program, u32 prg_rom_offset);
u32 program_mapped_instruction_offset(const Program *program, NES_MapAddr mapped);
u32 program_mapped_instruction_count(Debugger *debugger);
b32 program_dump(Debugger *debugger, const char *path, Arena *scratch);
void program_refine(Debugger *debugger, u32 instruction_budget);
void program_reset(Debugger *debugger);
void program_observe_execution(Debugger *debugger, NES_SchedulerBoundary trace);
ProgramSlice program_slice(Debugger *debugger, Arena *arena, u32 first_instruction_index, u32 capacity);
b32 program_index_from_cpu_address(Debugger *debugger, u16 cpu_address, u32 *instruction_index);

#endif
