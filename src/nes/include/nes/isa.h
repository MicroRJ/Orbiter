#ifndef NES_ISA_H
#define NES_ISA_H

#include "base.h"

typedef enum
{
	IMP = 0,
	ACC, IMM, REL,
	ZPG, ZPX, ZPY,
	ABS, ABY, ABX,
	INX, INY,
	IND,
}
NES_AddressingMode;

typedef enum
{
	NES_OPCODE_OFFICIAL,
	NES_OPCODE_UNOFFICIAL,
	NES_OPCODE_UNSTABLE,
	NES_OPCODE_HALT,
}
NES_OpcodeClass;

typedef enum : u8
{
#define OPCODE(CODE, SYMBOL, MNEMONIC, MODE, SIZE, CYCLES, PAGE, BRANCH, CLASS) SYMBOL = CODE,
#include "nes/opcodes.inc"
#undef OPCODE
}
NES_Opcode;

typedef struct
{
	const char *name;
	NES_AddressingMode mode;
	NES_OpcodeClass classification;
	u8 size;
	u8 cycles;
	u8 page_cross_cycles;
	u8 branch_taken_cycles;
}
NES_InstructionDesc;

#define OPCODE(CODE, SYMBOL, MNEMONIC, MODE, SIZE, CYCLES, PAGE, BRANCH, CLASS) \
	[CODE] = { #MNEMONIC, MODE, CLASS, SIZE, CYCLES, PAGE, BRANCH },
global const NES_InstructionDesc nes_instruction_descs[256] =
{
#include "nes/opcodes.inc"
};
#undef OPCODE

static inline NES_InstructionDesc nes_instruction_desc(u32 opcode)
{
	Assert(opcode < ArrayCount(nes_instruction_descs));
	return nes_instruction_descs[opcode];
}

#endif
