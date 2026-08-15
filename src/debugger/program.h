#ifndef DEBUGGER_PROGRAM_H
#define DEBUGGER_PROGRAM_H

#include "base.h"
#include "nes/emulator.h"

typedef struct NES_Process NES_Process;

enum
{
	PROGRAM_MAX_SIZE = NES_MAX_PRG_ROM_SIZE + NES_MAX_PRG_RAM_SIZE,
};

typedef enum
{
	PROGRAM_INSTRUCTION_STATIC   = 1,
	PROGRAM_INSTRUCTION_EXECUTED = 2,
}
ProgramInstructionFlags;

enum
{
	PROGRAM_ROW_INSTRUCTION,
	PROGRAM_ROW_GUESS,
	PROGRAM_ROW_ERROR,
};

typedef u8 ProgramRowStatus;

typedef struct
{
	NES_MapAddr map_addr;
	u16 cpu_address;
	i16 indent;
	u16 bridges;
	u16 data;
	u8 type;
	u8 flags;
	ProgramRowStatus status;
}
ProgramInstruction;

typedef struct
{
	u32                prg_rom_byte_count;
	u32                prg_ram_byte_count;
	u32                row_count;
	b32                listing_dirty;
	u16                cpu_pc;
	NES_MapAddr        cpu_pc_mapping;
	ProgramInstruction rows[NES_CPU_ADDRESS_SPACE];
}
Program;

b32 program_index_from_cpu_address(const Program *program, u16 cpu_address, u32 *instruction_index);

b32 program_dump(const Program *program, const char *path);
void program_invalidate(Program *program);
void program_reset(Program *program, u32 prg_rom_byte_count, u32 prg_ram_byte_count);
void program_rebuild(Program *program, NES_Emulator *emulator, const u8 *evidence);

#endif
