#include "cpu.h"
#include "../emulator_internal.h"

typedef struct
{
	u32 address;
	b32 page_crossed;
}
CPU_IndexedAddress;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bus access

static inline u32 cpu_read(NES_Emulator *core, u32 address)
{
	Assert(address <= MAX_VALUE_U16);
	return nes_cpu_bus_read(core, address);
}

static inline void cpu_write(NES_Emulator *core, u32 address, u32 value)
{
	Assert(address <= MAX_VALUE_U16);
	nes_cpu_bus_write(core, address, (u8)value);
}

static inline u32 cpu_read_word(NES_Emulator *core, u32 address)
{
	u32 low = cpu_read(core, address);
	u32 high = cpu_read(core, (address + 1) & MAX_VALUE_U16);
	return low | (high << 8);
}

static inline u32 cpu_read_zero_page_word(NES_Emulator *core, u32 address)
{
	u32 low = cpu_read(core, address & 0xFF);
	u32 high = cpu_read(core, (address + 1) & 0xFF);
	return low | (high << 8);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instruction stream and addressing

static inline u32 fetch_byte(NES_Emulator *core)
{
	NES_CPUState *cpu = &core->cpu;
	u32 value = cpu_read(core, cpu->PC);
	core->cpu.PC = (u16)(cpu->PC + 1);
	return value;
}

static inline u32 fetch_word(NES_Emulator *core)
{
	u32 low = fetch_byte(core);
	u32 high = fetch_byte(core);
	return low | (high << 8);
}

static inline CPU_IndexedAddress cpu_index_address(u32 base, u32 index)
{
	u32 address = (base + index) & MAX_VALUE_U16;
	return (CPU_IndexedAddress)
	{
		.address = address,
		.page_crossed = (base & 0xFF00) != (address & 0xFF00),
	};
}

static inline u32 zero_page(NES_Emulator *core)
{
	return fetch_byte(core);
}

static inline u32 zero_page_x(NES_Emulator *core)
{
	return (fetch_byte(core) + core->cpu.X) & 0xFF;
}

static inline u32 zero_page_y(NES_Emulator *core)
{
	return (fetch_byte(core) + core->cpu.Y) & 0xFF;
}

static inline u32 addr_abs(NES_Emulator *core)
{
	return fetch_word(core);
}

static inline CPU_IndexedAddress abs_x(NES_Emulator *core)
{
	return cpu_index_address(fetch_word(core), core->cpu.X);
}

static inline CPU_IndexedAddress abs_y(NES_Emulator *core)
{
	return cpu_index_address(fetch_word(core), core->cpu.Y);
}

static inline u32 ind_x(NES_Emulator *core)
{
	u32 pointer = (fetch_byte(core) + core->cpu.X) & 0xFF;
	return cpu_read_zero_page_word(core, pointer);
}

static inline CPU_IndexedAddress ind_y(NES_Emulator *core)
{
	u32 pointer = fetch_byte(core);
	u32 base = cpu_read_zero_page_word(core, pointer);
	return cpu_index_address(base, core->cpu.Y);
}

static inline u32 cpu_address_jmp_indirect(NES_Emulator *core)
{
	u32 pointer = fetch_word(core);
	u32 next = (pointer & 0xFF00) | ((pointer + 1) & 0xFF);
	return cpu_read(core, pointer) | (cpu_read(core, next) << 8);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status and stack

static inline b32 cpu_get_flag(const NES_Emulator *core, u32 flag)
{
	return !!(core->cpu.P & cpu_status_mask(flag));
}

static inline u32 cpu_status_with_flag(u32 status, u32 flag, b32 value)
{
	u32 mask = cpu_status_mask(flag);
	return (status & ~mask) | ((0u - !!value) & mask);
}

static inline u32 cpu_status_with_negative_and_zero(u32 status, u32 value)
{
	value &= 0xFF;
	status = cpu_status_with_flag(status, CPU_STAT_Z, value == 0);
	return cpu_status_with_flag(status, CPU_STAT_N, value & 0x80);
}

static inline void cpu_push_byte(NES_Emulator *core, u32 value)
{
	NES_CPUState *cpu = &core->cpu;
	cpu_write(core, 0x0100 | cpu->S, value);
	core->cpu.S = (u8)(cpu->S - 1);
}

static inline u32 cpu_pop_byte(NES_Emulator *core)
{
	NES_CPUState *cpu = &core->cpu;
	core->cpu.S = (u8)(cpu->S + 1);
	return cpu_read(core, 0x0100 | cpu->S);
}

static inline void cpu_push_word(NES_Emulator *core, u32 value)
{
	cpu_push_byte(core, value >> 8);
	cpu_push_byte(core, value);
}

static inline u32 cpu_pop_word(NES_Emulator *core)
{
	u32 low = cpu_pop_byte(core);
	u32 high = cpu_pop_byte(core);
	return low | (high << 8);
}

static inline u32 cpu_status_for_push(const NES_CPUState *cpu, b32 break_flag)
{
	u32 status = cpu->P | cpu_status_mask(CPU_STAT_1);
	if (break_flag)
	{
		status |= cpu_status_mask(CPU_STAT_B);
	}
	else
	{
		status &= ~cpu_status_mask(CPU_STAT_B);
	}
	return status;
}

static inline void cpu_restore_status(NES_Emulator *core, u32 status)
{
	core->cpu.P = (u8)((status | cpu_status_mask(CPU_STAT_1)) & ~cpu_status_mask(CPU_STAT_B));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instruction operations

static inline void cpu_load_a(NES_Emulator *core, u32 value)
{
	core->cpu.A = (u8)(value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.A));
}

static inline void cpu_load_x(NES_Emulator *core, u32 value)
{
	core->cpu.X = (u8)(value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.X));
}

static inline void cpu_load_y(NES_Emulator *core, u32 value)
{
	core->cpu.Y = (u8)(value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.Y));
}

static inline void cpu_compare(NES_Emulator *core, u32 left, u32 right)
{
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_C, left >= right);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, left - right));
}

static inline void cpu_add_with_carry(NES_Emulator *core, u32 value)
{
	NES_CPUState *cpu = &core->cpu;
	u32 accumulator = cpu->A;
	u32 result = accumulator + (value & 0xFF) + cpu_get_flag(core, CPU_STAT_C);

	u32 status = cpu_status_with_flag(cpu->P, CPU_STAT_C, result > 0xFF);
	status = cpu_status_with_flag(status, CPU_STAT_V, (~(accumulator ^ value) & (accumulator ^ result) & 0x80) != 0);
	core->cpu.A = (u8)(result);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, cpu->A));
}

static inline void cpu_subtract_with_carry(NES_Emulator *core, u32 value)
{
	cpu_add_with_carry(core, (~value) & 0xFF);
}

static inline void cpu_and(NES_Emulator *core, u32 value)
{
	core->cpu.A = (u8)(core->cpu.A & value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.A));
}

static inline void cpu_or(NES_Emulator *core, u32 value)
{
	core->cpu.A = (u8)(core->cpu.A | value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.A));
}

static inline void cpu_xor(NES_Emulator *core, u32 value)
{
	core->cpu.A = (u8)(core->cpu.A ^ value);
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, core->cpu.A));
}

static inline void cpu_test_bits(NES_Emulator *core, u32 value)
{
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_Z, (core->cpu.A & value) == 0);
	status = cpu_status_with_flag(status, CPU_STAT_V, value & 0x40);
	core->cpu.P = (u8)(cpu_status_with_flag(status, CPU_STAT_N, value & 0x80));
}

static inline u32 cpu_shift_left(NES_Emulator *core, u32 value)
{
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_C, value & 0x80);
	value = (value << 1) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, value));
	return value;
}

static inline u32 cpu_shift_right(NES_Emulator *core, u32 value)
{
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_C, value & 0x01);
	value = (value >> 1) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, value));
	return value;
}

static inline u32 cpu_rotate_left(NES_Emulator *core, u32 value)
{
	u32 carry_in = cpu_get_flag(core, CPU_STAT_C);
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_C, value & 0x80);
	value = ((value << 1) | carry_in) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, value));
	return value;
}

static inline u32 cpu_rotate_right(NES_Emulator *core, u32 value)
{
	u32 carry_in = cpu_get_flag(core, CPU_STAT_C) << 7;
	u32 status = cpu_status_with_flag(core->cpu.P, CPU_STAT_C, value & 0x01);
	value = (carry_in | (value >> 1)) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(status, value));
	return value;
}

static inline u32 cpu_increment(NES_Emulator *core, u32 value)
{
	value = (value + 1) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, value));
	return value;
}

static inline u32 cpu_decrement(NES_Emulator *core, u32 value)
{
	value = (value - 1) & 0xFF;
	core->cpu.P = (u8)(cpu_status_with_negative_and_zero(core->cpu.P, value));
	return value;
}

static inline u32 cpu_branch(NES_Emulator *core, b32 condition)
{
	NES_CPUState *cpu = &core->cpu;
	i32 offset = (i8)fetch_byte(core);
	u32 cycles = 2;

	if (condition)
	{
		u32 previous_pc = cpu->PC;
		core->cpu.PC = (u16)(cpu->PC + offset);
		cycles += 1;
		cycles += (previous_pc & 0xFF00) != (cpu->PC & 0xFF00);
	}

	return cycles;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Interrupts and reset
//
//

static u16 nes_cpu_read_irq_vector(NES_Emulator *core)
{
	return cpu_read_word(core, 0xFFFE);
}

static u16 nes_cpu_read_nmi_vector(NES_Emulator *core)
{
	return cpu_read_word(core, 0xFFFA);
}

static void nes_cpu_interrupt(NES_Emulator *core, u32 address)
{
	NES_CPUState *cpu = &core->cpu;
	cpu_push_word(core, cpu->PC);
	cpu_push_byte(core, cpu_status_for_push(cpu, false));
	core->cpu.P = (u8)(cpu->P | cpu_status_mask(CPU_STAT_I));
	core->cpu.PC = (u16)(address);
}

// INTERRUPT REQUEST
// https://www.nesdev.org/wiki/IRQ
// """
// If the CPU's /IRQ input is 0 at the end of an instruction, then the CPU pushes the program counter and
// the processor status register, sets the I flag to ignore further IRQs, and the Program Counter takes
// the value read at $fffe and $ffff.
// This behaviour is masked by the CPU's interrupt-disable status flag. The SEI instruction disables IRQs,
// and the CLI instruction enables them.
// """
// Todo, actually defer this, and it works such that IRQ is one, but when set to zero, at the end of an
// instruction then we do this ...
//
u32 nes_cpu_irq(NES_Emulator *core)
{
	nes_cpu_interrupt(core, nes_cpu_read_irq_vector(core));
	return 7;
}


// NON MASKABLE INTERRUPT
// https://www.nesdev.org/wiki/NMI
// """
// The 2A03 and most other 6502 family CPUs are capable of processing a non-maskable interrupt (NMI).
// This calls a special NMI handler function after the current instruction ends.
// The check for this is edge-sensitive, meaning that if other circuitry on the board pulls the /NMI
// pin from high to low voltage, this sets a flip-flop in the CPU.
// When the CPU checks for interrupts and find that the flip-flop is set, it pushes the current state
// onto the stack and jumps to the NMI handler address at $FFFA-$FFFB.
// "Non-maskable" means that no state inside the CPU can prevent the NMI from being processed as an
// interrupt. However, most boards that use a 6502 CPU's /NMI line allow the CPU to disable the
// generation of /NMI signals by writing to a memory-mapped I/O device. In the case of the NES, the
// /NMI line is connected to the NES PPU and is used to detect vertical blanking.
// """
//
void nes_cpu_nmi(NES_Emulator *core)
{
	nes_cpu_interrupt(core, nes_cpu_read_nmi_vector(core));
}



void nes_cpu_power_on(NES_Emulator *core)
{
	memory_zero(&core->cpu, sizeof(core->cpu));
	nes_cpu_reset(core);
}

void nes_cpu_reset(NES_Emulator *core)
{
	NES_CPUState *cpu = &core->cpu;
	cpu->S -= 3;
	cpu->P |= cpu_status_mask(CPU_STAT_I) | cpu_status_mask(CPU_STAT_1);
	cpu->PC = cpu_read_word(core, RESET_VECTOR);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Execution

u32 nes_cpu_step(NES_Emulator *core)
{
	// Todo, remove this from here, put in the scheduler?
	core->cpu_stall_cycles = 0;

	NES_CPUState *cpu = &core->cpu;

	u32 opcode = fetch_byte(core);
	u32 cycles = 0;

	u32                  value;
	u32                address;
	CPU_IndexedAddress indexed;

	switch (opcode)
	{
		// Load accumulator
		case 0xA9: cpu_load_a(core, fetch_byte(core)); cycles = 2; break;
		case 0xA5: cpu_load_a(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xB5: cpu_load_a(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0xAD: cpu_load_a(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xBD: indexed = abs_x(core); cpu_load_a(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xB9: indexed = abs_y(core); cpu_load_a(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xA1: cpu_load_a(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0xB1: indexed = ind_y(core); cpu_load_a(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		// Load X
		case 0xA2: cpu_load_x(core, fetch_byte(core)); cycles = 2; break;
		case 0xA6: cpu_load_x(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xB6: cpu_load_x(core, cpu_read(core, zero_page_y(core))); cycles = 4; break;
		case 0xAE: cpu_load_x(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xBE: indexed = abs_y(core); cpu_load_x(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;

		// Load Y
		case 0xA0: cpu_load_y(core, fetch_byte(core)); cycles = 2; break;
		case 0xA4: cpu_load_y(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xB4: cpu_load_y(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0xAC: cpu_load_y(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xBC: indexed = abs_x(core); cpu_load_y(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;

		// Store registers
		case 0x85: cpu_write(core, zero_page(core), cpu->A); cycles = 3; break;
		case 0x95: cpu_write(core, zero_page_x(core), cpu->A); cycles = 4; break;
		case 0x8D: cpu_write(core, addr_abs(core), cpu->A); cycles = 4; break;
		case 0x9D: indexed = abs_x(core); cpu_write(core, indexed.address, cpu->A); cycles = 5; break;
		case 0x99: indexed = abs_y(core); cpu_write(core, indexed.address, cpu->A); cycles = 5; break;
		case 0x81: cpu_write(core, ind_x(core), cpu->A); cycles = 6; break;
		case 0x91: indexed = ind_y(core); cpu_write(core, indexed.address, cpu->A); cycles = 6; break;
		case 0x86: cpu_write(core, zero_page(core), cpu->X); cycles = 3; break;
		case 0x96: cpu_write(core, zero_page_y(core), cpu->X); cycles = 4; break;
		case 0x8E: cpu_write(core, addr_abs(core), cpu->X); cycles = 4; break;
		case 0x84: cpu_write(core, zero_page(core), cpu->Y); cycles = 3; break;
		case 0x94: cpu_write(core, zero_page_x(core), cpu->Y); cycles = 4; break;
		case 0x8C: cpu_write(core, addr_abs(core), cpu->Y); cycles = 4; break;

		// Add with carry
		case 0x69: cpu_add_with_carry(core, fetch_byte(core)); cycles = 2; break;
		case 0x65: cpu_add_with_carry(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0x75: cpu_add_with_carry(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0x6D: cpu_add_with_carry(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0x7D: indexed = abs_x(core); cpu_add_with_carry(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x79: indexed = abs_y(core); cpu_add_with_carry(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x61: cpu_add_with_carry(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0x71: indexed = ind_y(core); cpu_add_with_carry(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		// Subtract with carry
		case 0xE9: cpu_subtract_with_carry(core, fetch_byte(core)); cycles = 2; break;
		case 0xE5: cpu_subtract_with_carry(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xF5: cpu_subtract_with_carry(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0xED: cpu_subtract_with_carry(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xFD: indexed = abs_x(core); cpu_subtract_with_carry(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xF9: indexed = abs_y(core); cpu_subtract_with_carry(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xE1: cpu_subtract_with_carry(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0xF1: indexed = ind_y(core); cpu_subtract_with_carry(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		// Logical operations
		case 0x29: cpu_and(core, fetch_byte(core)); cycles = 2; break;
		case 0x25: cpu_and(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0x35: cpu_and(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0x2D: cpu_and(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0x3D: indexed = abs_x(core); cpu_and(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x39: indexed = abs_y(core); cpu_and(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x21: cpu_and(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0x31: indexed = ind_y(core); cpu_and(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		case 0x09: cpu_or(core, fetch_byte(core)); cycles = 2; break;
		case 0x05: cpu_or(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0x15: cpu_or(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0x0D: cpu_or(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0x1D: indexed = abs_x(core); cpu_or(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x19: indexed = abs_y(core); cpu_or(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x01: cpu_or(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0x11: indexed = ind_y(core); cpu_or(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		case 0x49: cpu_xor(core, fetch_byte(core)); cycles = 2; break;
		case 0x45: cpu_xor(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0x55: cpu_xor(core, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0x4D: cpu_xor(core, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0x5D: indexed = abs_x(core); cpu_xor(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x59: indexed = abs_y(core); cpu_xor(core, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0x41: cpu_xor(core, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0x51: indexed = ind_y(core); cpu_xor(core, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;

		case 0x24: cpu_test_bits(core, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0x2C: cpu_test_bits(core, cpu_read(core, addr_abs(core))); cycles = 4; break;

		// Compare registers
		case 0xC9: cpu_compare(core, cpu->A, fetch_byte(core)); cycles = 2; break;
		case 0xC5: cpu_compare(core, cpu->A, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xD5: cpu_compare(core, cpu->A, cpu_read(core, zero_page_x(core))); cycles = 4; break;
		case 0xCD: cpu_compare(core, cpu->A, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xDD: indexed = abs_x(core); cpu_compare(core, cpu->A, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xD9: indexed = abs_y(core); cpu_compare(core, cpu->A, cpu_read(core, indexed.address)); cycles = 4 + indexed.page_crossed; break;
		case 0xC1: cpu_compare(core, cpu->A, cpu_read(core, ind_x(core))); cycles = 6; break;
		case 0xD1: indexed = ind_y(core); cpu_compare(core, cpu->A, cpu_read(core, indexed.address)); cycles = 5 + indexed.page_crossed; break;
		case 0xE0: cpu_compare(core, cpu->X, fetch_byte(core)); cycles = 2; break;
		case 0xE4: cpu_compare(core, cpu->X, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xEC: cpu_compare(core, cpu->X, cpu_read(core, addr_abs(core))); cycles = 4; break;
		case 0xC0: cpu_compare(core, cpu->Y, fetch_byte(core)); cycles = 2; break;
		case 0xC4: cpu_compare(core, cpu->Y, cpu_read(core, zero_page(core))); cycles = 3; break;
		case 0xCC: cpu_compare(core, cpu->Y, cpu_read(core, addr_abs(core))); cycles = 4; break;

		// Shifts and rotates
		case 0x0A: cpu->A = (u8)(cpu_shift_left(core, cpu->A)); cycles = 2; break;
		case 0x06: address = zero_page(core); value = cpu_shift_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0x16: address = zero_page_x(core); value = cpu_shift_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x0E: address = addr_abs(core); value = cpu_shift_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x1E: indexed = abs_x(core); value = cpu_shift_left(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;

		case 0x4A: cpu->A = (u8)(cpu_shift_right(core, cpu->A)); cycles = 2; break;
		case 0x46: address = zero_page(core); value = cpu_shift_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0x56: address = zero_page_x(core); value = cpu_shift_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x4E: address = addr_abs(core); value = cpu_shift_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x5E: indexed = abs_x(core); value = cpu_shift_right(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;

		case 0x2A: cpu->A = (u8)(cpu_rotate_left(core, cpu->A)); cycles = 2; break;
		case 0x26: address = zero_page(core); value = cpu_rotate_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0x36: address = zero_page_x(core); value = cpu_rotate_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x2E: address = addr_abs(core); value = cpu_rotate_left(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x3E: indexed = abs_x(core); value = cpu_rotate_left(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;

		case 0x6A: cpu->A = (u8)(cpu_rotate_right(core, cpu->A)); cycles = 2; break;
		case 0x66: address = zero_page(core); value = cpu_rotate_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0x76: address = zero_page_x(core); value = cpu_rotate_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x6E: address = addr_abs(core); value = cpu_rotate_right(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0x7E: indexed = abs_x(core); value = cpu_rotate_right(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;

		// Increment and decrement memory
		case 0xE6: address = zero_page(core); value = cpu_increment(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0xF6: address = zero_page_x(core); value = cpu_increment(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0xEE: address = addr_abs(core); value = cpu_increment(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0xFE: indexed = abs_x(core); value = cpu_increment(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;
		case 0xC6: address = zero_page(core); value = cpu_decrement(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 5; break;
		case 0xD6: address = zero_page_x(core); value = cpu_decrement(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0xCE: address = addr_abs(core); value = cpu_decrement(core, cpu_read(core, address)); cpu_write(core, address, value); cycles = 6; break;
		case 0xDE: indexed = abs_x(core); value = cpu_decrement(core, cpu_read(core, indexed.address)); cpu_write(core, indexed.address, value); cycles = 7; break;

		// Register transfers and increments
		case 0xAA: cpu_load_x(core, cpu->A); cycles = 2; break;
		case 0x8A: cpu_load_a(core, cpu->X); cycles = 2; break;
		case 0xA8: cpu_load_y(core, cpu->A); cycles = 2; break;
		case 0x98: cpu_load_a(core, cpu->Y); cycles = 2; break;
		case 0xBA: cpu_load_x(core, cpu->S); cycles = 2; break;
		case 0x9A: cpu->S = (u8)(cpu->X); cycles = 2; break;
		case 0xE8: cpu->X = (u8)(cpu->X + 1); cpu->P = (u8)(cpu_status_with_negative_and_zero(cpu->P, cpu->X)); cycles = 2; break;
		case 0xCA: cpu->X = (u8)(cpu->X - 1); cpu->P = (u8)(cpu_status_with_negative_and_zero(cpu->P, cpu->X)); cycles = 2; break;
		case 0xC8: cpu->Y = (u8)(cpu->Y + 1); cpu->P = (u8)(cpu_status_with_negative_and_zero(cpu->P, cpu->Y)); cycles = 2; break;
		case 0x88: cpu->Y = (u8)(cpu->Y - 1); cpu->P = (u8)(cpu_status_with_negative_and_zero(cpu->P, cpu->Y)); cycles = 2; break;

		// Status flags
		case 0x18: cpu->P = (u8)(cpu->P & ~cpu_status_mask(CPU_STAT_C)); cycles = 2; break;
		case 0x38: cpu->P = (u8)(cpu->P | cpu_status_mask(CPU_STAT_C)); cycles = 2; break;
		case 0x58: cpu->P = (u8)(cpu->P & ~cpu_status_mask(CPU_STAT_I)); cycles = 2; break;
		case 0x78: cpu->P = (u8)(cpu->P | cpu_status_mask(CPU_STAT_I)); cycles = 2; break;
		case 0xD8: cpu->P = (u8)(cpu->P & ~cpu_status_mask(CPU_STAT_D)); cycles = 2; break;
		case 0xF8: cpu->P = (u8)(cpu->P | cpu_status_mask(CPU_STAT_D)); cycles = 2; break;
		case 0xB8: cpu->P = (u8)(cpu->P & ~cpu_status_mask(CPU_STAT_V)); cycles = 2; break;

		// Branches
		case 0x10: cycles = cpu_branch(core, !cpu_get_flag(core, CPU_STAT_N)); break;
		case 0x30: cycles = cpu_branch(core, cpu_get_flag(core, CPU_STAT_N)); break;
		case 0x50: cycles = cpu_branch(core, !cpu_get_flag(core, CPU_STAT_V)); break;
		case 0x70: cycles = cpu_branch(core, cpu_get_flag(core, CPU_STAT_V)); break;
		case 0x90: cycles = cpu_branch(core, !cpu_get_flag(core, CPU_STAT_C)); break;
		case 0xB0: cycles = cpu_branch(core, cpu_get_flag(core, CPU_STAT_C)); break;
		case 0xD0: cycles = cpu_branch(core, !cpu_get_flag(core, CPU_STAT_Z)); break;
		case 0xF0: cycles = cpu_branch(core, cpu_get_flag(core, CPU_STAT_Z)); break;

		// Stack
		case 0x48: cpu_push_byte(core, cpu->A); cycles = 3; break;
		case 0x68: cpu_load_a(core, cpu_pop_byte(core)); cycles = 4; break;
		case 0x08: cpu_push_byte(core, cpu_status_for_push(cpu, true)); cycles = 3; break;
		case 0x28: cpu_restore_status(core, cpu_pop_byte(core)); cycles = 4; break;

		// Control flow
		case 0x4C: cpu->PC = (u16)(addr_abs(core)); cycles = 3; break;
		case 0x6C: cpu->PC = (u16)(cpu_address_jmp_indirect(core)); cycles = 5; break;
		case 0x20: address = addr_abs(core); cpu_push_word(core, cpu->PC - 1); cpu->PC = (u16)(address); cycles = 6; break;
		case 0x60: cpu->PC = (u16)(cpu_pop_word(core) + 1); cycles = 6; break;
		case 0x40: cpu_restore_status(core, cpu_pop_byte(core)); cpu->PC = (u16)(cpu_pop_word(core)); cycles = 6; break;
		case 0x00:
			cpu_push_word(core, cpu->PC + 1);
			cpu_push_byte(core, cpu_status_for_push(cpu, true));
			core->cpu.P = (u8)(cpu->P | cpu_status_mask(CPU_STAT_I));
			core->cpu.PC = (u16)(cpu_read_word(core, 0xFFFE));
			cycles = 7;
			break;

		case 0xEA: cycles = 2; break;

		default:
		{
			Assert(cpu->PC >= 1);
			LOG_ERROR("%02X %04X %s: UNIMPLEMENTED INSTRUCTION", opcode, cpu->PC - 1, nes_instruction_desc(opcode).name);
			Assert(!"unimplemented CPU opcode");
			return 0;
		}
		break;
	}

	return cycles + core->cpu_stall_cycles;
}
