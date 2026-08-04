#include "debugger_internal.h"
#include "nes/isa.h"
#include "program.h"

typedef struct
{
	u16 address;
}
ProgramWork;

typedef enum
{
	PROGRAM_MARK_CONFLICT,
	PROGRAM_MARK_EXISTING,
	PROGRAM_MARK_ADDED,
}
ProgramMarkResult;

typedef struct
{
	u16 type;
	u16 data;
}
ProgramDecodedInstruction;

typedef struct
{
	Debugger *debugger;
	u32 cursor;
}
ProgramIterator;

static u32 program_instruction_storage_offset(const Program *program, u32 storage_offset);
static void program_clear_instruction(Program *program, u32 start_offset);

static b32 program_storage_offset_from_map(const Program *program, NES_MapAddr mapped, u32 *storage_offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count) {
		*storage_offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count) {
		*storage_offset = program->prg_rom_byte_count + mapped.offset;
		return true;
	}
	return false;
}

static b32 program_is_analyzed_map(const Program *program, NES_MapAddr mapped)
{
	u32 storage_offset;
	return program_storage_offset_from_map(program, mapped, &storage_offset);
}

static void program_update_cached_byte(Program *program, NES_MapAddr mapped, u32 storage_offset, u8 value)
{
	if (mapped.device != NES_DEVICE_PRG_RAM) {
		return;
	}
	ProgramByte *byte = &program->bytes[storage_offset];
	if (!byte->value_valid)
	{
		byte->value = value;
		byte->value_valid = true;
		return;
	}
	if (byte->value == value) {
		return;
	}
	u32 ram_begin = program->prg_rom_byte_count;
	u32 first = storage_offset >= ram_begin + 2 ? storage_offset - 2 : ram_begin;
	u32 cleared[3] = { MAX_VALUE_U32, MAX_VALUE_U32, MAX_VALUE_U32 };
	u32 cleared_count = 0;
	for (u32 candidate = first; candidate <= storage_offset; ++candidate)
	{
		u32 start = program_instruction_storage_offset(program, candidate);
		if (start == MAX_VALUE_U32) {
			continue;
		}
		b32 duplicate = false;
		for (u32 index = 0; index < cleared_count; ++index) {
			duplicate |= cleared[index] == start;
		}
		if (!duplicate) {
			cleared[cleared_count++] = start;
		}
	}
	for (u32 index = 0; index < cleared_count; ++index) {
		program_clear_instruction(program, cleared[index]);
	}
	byte->value = value;
	byte->value_valid = true;
}

static void program_sync_byte(Debugger *debugger, u16 cpu_address, NES_MapAddr mapped, u32 storage_offset)
{
	program_update_cached_byte(&debugger->program, mapped, storage_offset, nes_emulator_cpu_peek(debugger->emulator, cpu_address));
}

static ProgramDecodedInstruction program_decode_instruction(Debugger *debugger, u16 address)
{
	return (ProgramDecodedInstruction) {
		.type = nes_emulator_cpu_peek(debugger->emulator, address),
		.data = nes_emulator_cpu_peek_word(debugger->emulator, (u16)(address + 1)),
	};
}

static ProgramIterator program_iterator(Debugger *debugger, u32 cpu_offset)
{
	Assert(debugger);
	Assert(cpu_offset <= NES_CPU_ADDRESS_SPACE);
	return (ProgramIterator) { .debugger = debugger, .cursor = cpu_offset };
}

static b32 program_iterator_next(ProgramIterator *iterator, ProgramInstruction *instruction)
{
	Assert(iterator);
	Assert(iterator->debugger);
	Assert(instruction);
	Debugger *debugger = iterator->debugger;
	while (iterator->cursor < NES_CPU_ADDRESS_SPACE)
	{
		u16 cpu_address = (u16)iterator->cursor;
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, cpu_address);
		u32 storage_offset = 0;
		if (!program_storage_offset_from_map(&debugger->program, mapped, &storage_offset))
		{
			++iterator->cursor;
			continue;
		}
		program_sync_byte(debugger, cpu_address, mapped, storage_offset);

		ProgramDecodedInstruction decoded = program_decode_instruction(debugger, cpu_address);
		u32 size = nes_instruction_desc(decoded.type).size;
		for (u32 byte_index = 1; byte_index < size; ++byte_index)
		{
			u16 operand_address = (u16)(cpu_address + byte_index);
			NES_MapAddr operand = nes_emulator_cpu_map(debugger->emulator, operand_address);
			u32 operand_offset = 0;
			if (program_storage_offset_from_map(&debugger->program, operand, &operand_offset)) {
				program_sync_byte(debugger, operand_address, operand, operand_offset);
			}
		}

		u32 instruction_offset = program_instruction_storage_offset(&debugger->program, storage_offset);
		if (instruction_offset != MAX_VALUE_U32 && instruction_offset != storage_offset)
		{
			++iterator->cursor;
			continue;
		}

		u32 advance = size;
		ProgramRowStatus status = PROGRAM_ROW_INSTRUCTION;
		if (instruction_offset == MAX_VALUE_U32)
		{
			status = PROGRAM_ROW_GUESS;
			for (u32 byte_index = 1; byte_index < size; ++byte_index)
			{
				NES_MapAddr operand = nes_emulator_cpu_map(debugger->emulator, (u16)(cpu_address + byte_index));
				u32 operand_offset = 0;
				if (!program_storage_offset_from_map(&debugger->program, operand, &operand_offset) || program_instruction_storage_offset(&debugger->program, operand_offset) != MAX_VALUE_U32)
				{
					advance = 1;
					status = PROGRAM_ROW_ERROR;
					break;
				}
			}
		}
		*instruction = (ProgramInstruction) {
			.map_addr = mapped,
			.cpu_address = cpu_address,
			.type = decoded.type,
			.data = decoded.data,
			.advance = (u16)advance,
			.status = status,
		};
		iterator->cursor += advance;
		return true;
	}
	return false;
}

// TODO: profile this!
// TODO: we can binary search this!
static b32 program_cpu_address_from_index(Debugger *debugger, u32 instruction_index, u16 *cpu_address)
{
	Assert(debugger);
	Assert(cpu_address);
	Program *program = &debugger->program;
	u32 remaining = instruction_index;
	for (u32 cpu_base = 0; cpu_base < NES_CPU_ADDRESS_SPACE; cpu_base += PROGRAM_BUCKET_SIZE)
	{
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, (u16)cpu_base);
		u32 storage_base = 0;
		if (!program_storage_offset_from_map(program, mapped, &storage_base)) {
			continue;
		}
		Assert(storage_base % PROGRAM_BUCKET_SIZE == 0);
		u32 bucket = storage_base / PROGRAM_BUCKET_SIZE;
		Assert(bucket < program->instruction_bucket_count);
		u32 count = program->instruction_buckets[bucket];
		if (remaining >= count)
		{
			remaining -= count;
			continue;
		}
		for (u32 byte_index = 0; byte_index < PROGRAM_BUCKET_SIZE; ++byte_index)
		{
			u32 program_offset = storage_base + byte_index;
			if (program_offset >= program->byte_count) {
				break;
			}
			u32 start_offset = program_instruction_storage_offset(program, program_offset);
			if (start_offset != MAX_VALUE_U32 && start_offset != program_offset) {
				continue;
			}
			if (remaining)
			{
				--remaining;
				continue;
			}
			*cpu_address = (u16)(cpu_base + byte_index);
			return true;
		}
		return false;
	}
	return false;
}

b32 program_index_from_cpu_address(Debugger *debugger, u16 cpu_address, u32 *instruction_index)
{
	Assert(debugger);
	Assert(instruction_index);
	Program *program = &debugger->program;
	u32 result = 0;
	u32 cpu_base = cpu_address & ~(PROGRAM_BUCKET_SIZE - 1);
	for (u32 preceding_base = 0; preceding_base < cpu_base; preceding_base += PROGRAM_BUCKET_SIZE)
	{
		NES_MapAddr preceding = nes_emulator_cpu_map(debugger->emulator, (u16)preceding_base);
		u32 preceding_offset = 0;
		if (!program_storage_offset_from_map(program, preceding, &preceding_offset)) {
			continue;
		}
		Assert(preceding_offset % PROGRAM_BUCKET_SIZE == 0);
		u32 bucket = preceding_offset / PROGRAM_BUCKET_SIZE;
		Assert(bucket < program->instruction_bucket_count);
		result += program->instruction_buckets[bucket];
	}
	NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, cpu_address);
	u32 mapped_offset = 0;
	if (!program_storage_offset_from_map(program, mapped, &mapped_offset)) {
		return false;
	}
	NES_MapAddr mapped_base = nes_emulator_cpu_map(debugger->emulator, (u16)cpu_base);
	u32 mapped_base_offset = 0;
	if (!program_storage_offset_from_map(program, mapped_base, &mapped_base_offset) || mapped_offset < mapped_base_offset) {
		return false;
	}
	for (u32 program_offset = mapped_base_offset; program_offset < mapped_offset; ++program_offset)
	{
		u32 start_offset = program_instruction_storage_offset(program, program_offset);
		if (start_offset == MAX_VALUE_U32 || start_offset == program_offset) {
			++result;
		}
	}
	u32 start_offset = program_instruction_storage_offset(program, mapped_offset);
	if (start_offset != MAX_VALUE_U32 && start_offset != mapped_offset) {
		return false;
	}
	*instruction_index = result;
	return true;
}

ProgramSlice program_slice(Debugger *debugger, Arena *arena, u32 first_instruction_index, u32 capacity)
{
	ProgramSlice result = {};
	if (!capacity) {
		return result;
	}
	u16 first_cpu_address = 0;
	if (!program_cpu_address_from_index(debugger, first_instruction_index, &first_cpu_address)) {
		return result;
	}
	ProgramIterator iterator = program_iterator(debugger, first_cpu_address);
	result.items = arena_push_zero(arena, capacity * sizeof(*result.items));
	while (result.count < capacity && program_iterator_next(&iterator, &result.items[result.count]))
	{
		ProgramInstruction *instruction = &result.items[result.count];
		u32 storage_offset = 0;
		Assert(program_storage_offset_from_map(&debugger->program, instruction->map_addr, &storage_offset));
		ProgramByte *layout = &debugger->program.bytes[storage_offset];
		instruction->bridges = layout->bridges;
		instruction->indent = (i16)layout->indent;
		++result.count;
	}
	Assert(!result.count || result.items[0].cpu_address == first_cpu_address);
	return result;
}

static u32 program_instruction_storage_offset(const Program *program, u32 storage_offset)
{
	if (storage_offset >= program->byte_count) return MAX_VALUE_U32;
	const ProgramByte *byte = &program->bytes[storage_offset];
	if (byte->offset_to_start == PROGRAM_BYTE_UNKNOWN) return MAX_VALUE_U32;
	Assert(byte->offset_to_start <= 2);
	Assert(storage_offset >= byte->offset_to_start);
	u32 instruction_offset = storage_offset - byte->offset_to_start;
	Assert(program->bytes[instruction_offset].offset_to_start == 0);
	return instruction_offset;
}

u32 program_instruction_offset(const Program *program, u32 prg_rom_offset)
{
	return prg_rom_offset < program->prg_rom_byte_count ? program_instruction_storage_offset(program, prg_rom_offset) : MAX_VALUE_U32;
}

u32 program_mapped_instruction_offset(const Program *program, NES_MapAddr mapped)
{
	u32 storage_offset = 0;
	return program_storage_offset_from_map(program, mapped, &storage_offset) ? program_instruction_storage_offset(program, storage_offset) : MAX_VALUE_U32;
}

ProgramTags program_tags(const Program *program, u32 prg_rom_offset)
{
	return program_instruction_offset(program, prg_rom_offset) == prg_rom_offset ? PROGRAM_INSTRUCTION : PROGRAM_NONE;
}

u32 program_mapped_instruction_count(Debugger *debugger)
{
	Program *program = &debugger->program;
	u32 result = 0;
	for (u32 chunk = 0; chunk < CPU_MAPPING_CHUNK_COUNT; ++chunk)
	{
		u16 cpu_address = (u16)(chunk * CPU_MAPPING_CHUNK_SIZE);
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, cpu_address);
		u32 storage_offset = 0;
		if (!program_storage_offset_from_map(program, mapped, &storage_offset)) {
			continue;
		}
		Assert(storage_offset % PROGRAM_BUCKET_SIZE == 0);
		for (u32 offset = storage_offset; offset < storage_offset + CPU_MAPPING_CHUNK_SIZE; offset += PROGRAM_BUCKET_SIZE)
		{
			u32 bucket = offset / PROGRAM_BUCKET_SIZE;
			Assert(bucket < program->instruction_bucket_count);
			result += program->instruction_buckets[bucket];
		}
	}
	return result;
}

static void program_clear_instruction(Program *program, u32 start_offset)
{
	Assert(start_offset < program->byte_count);
	ProgramByte *start = &program->bytes[start_offset];
	if (start->offset_to_start == PROGRAM_BYTE_UNKNOWN) return;
	Assert(start->offset_to_start == 0);
	start->offset_to_start = PROGRAM_BYTE_UNKNOWN;
	start->flags = 0;
	for (u32 byte_index = 1; byte_index < 3; ++byte_index)
	{
		u32 offset = start_offset + byte_index;
		if (offset >= program->byte_count) break;
		ProgramByte *byte = &program->bytes[offset];
		if (byte->offset_to_start != byte_index) break;
		byte->offset_to_start = PROGRAM_BYTE_UNKNOWN;
		byte->flags = 0;
		++program->instruction_buckets[offset / PROGRAM_BUCKET_SIZE];
	}
	--program->instruction_count;
	++program->revision;
}

static b32 program_offsets_contiguous(const u32 *offsets, u32 size)
{
	if (!size || offsets[0] == MAX_VALUE_U32) {
		return false;
	}
	for (u32 byte_index = 1; byte_index < size; ++byte_index) {
		if (offsets[byte_index] != offsets[0] + byte_index) {
			return false;
		}
	}
	return true;
}

static u32 program_marked_size(const Program *program, u32 start_offset)
{
	u32 size = 1;
	while (size < 3 && start_offset + size < program->byte_count &&
		program->bytes[start_offset + size].offset_to_start == size)
	{
		++size;
	}
	return size;
}

static void program_log_conflict(const Program *program, const u32 *offsets, u32 size, const u32 *conflicts, u32 conflict_count, b32 replaces)
{
	if (program->instruction_conflict_count > 8) {
		return;
	}
	const char *action = replaces ? "replaces" : "blocked by";
	const char *device = offsets[0] < program->prg_rom_byte_count ? "PRG ROM" : "PRG RAM";
	u32 base = offsets[0] < program->prg_rom_byte_count ? 0 : program->prg_rom_byte_count;
	if (conflict_count == 1) {
		LOG_DEBUG("instruction conflict at %s $%X-$%X %s existing start $%X", device, offsets[0] - base, offsets[size - 1] - base, action, conflicts[0] - base);
	} else if (conflict_count == 2) {
		LOG_DEBUG("instruction conflict at %s $%X-$%X %s existing starts $%X, $%X", device, offsets[0] - base, offsets[size - 1] - base, action, conflicts[0] - base, conflicts[1] - base);
	} else {
		LOG_DEBUG("instruction conflict at %s $%X-$%X %s existing starts $%X, $%X, $%X", device, offsets[0] - base, offsets[size - 1] - base, action, conflicts[0] - base, conflicts[1] - base, conflicts[2] - base);
	}
}

static void program_log_executed_conflict(const Program *program, u16 cpu_address, const u32 *offsets, u32 size, const u32 *conflicts, u32 conflict_count)
{
	if (program->executed_instruction_conflict_count > 8) {
		return;
	}
	const char *device = offsets[0] < program->prg_rom_byte_count ? "PRG ROM" : "PRG RAM";
	u32 base = offsets[0] < program->prg_rom_byte_count ? 0 : program->prg_rom_byte_count;
	if (conflict_count == 1) {
		LOG_ERROR("executed instruction conflict at CPU $%04X, %s $%X-$%X overlaps executed start $%X", cpu_address, device, offsets[0] - base, offsets[size - 1] - base, conflicts[0] - base);
	} else if (conflict_count == 2) {
		LOG_ERROR("executed instruction conflict at CPU $%04X, %s $%X-$%X overlaps executed starts $%X, $%X", cpu_address, device, offsets[0] - base, offsets[size - 1] - base, conflicts[0] - base, conflicts[1] - base);
	} else {
		LOG_ERROR("executed instruction conflict at CPU $%04X, %s $%X-$%X overlaps executed starts $%X, $%X, $%X", cpu_address, device, offsets[0] - base, offsets[size - 1] - base, conflicts[0] - base, conflicts[1] - base, conflicts[2] - base);
	}
}

static ProgramMarkResult program_mark_instruction(Program *program, const u32 *offsets, u32 size, u8 flags, b32 replace_conflicts, u16 cpu_address)
{
	u32 conflicts[3] = {};
	u32 conflict_count = 0;
	Assert(size && size <= 3);
	Assert(program_offsets_contiguous(offsets, size));

	u32 at_start = program_instruction_storage_offset(program, offsets[0]);
	if (at_start == offsets[0] && program_marked_size(program, offsets[0]) != size) {
		conflicts[conflict_count++] = offsets[0];
	}

	for (u32 byte_index = 0; byte_index < size; ++byte_index)
	{
		u32 offset = offsets[byte_index];
		Assert(offset < program->byte_count);

		u32 instruction_offset = program_instruction_storage_offset(program, offset);
		if (instruction_offset == MAX_VALUE_U32) continue;
		if (instruction_offset == offsets[0]) continue;

		b32 already_added = false;
		for (u32 index = 0; index < conflict_count; ++index) {
			already_added |= conflicts[index] == instruction_offset;
		}
		if (!already_added) conflicts[conflict_count++] = instruction_offset;
	}

	if (conflict_count && !replace_conflicts)
	{
		++program->instruction_conflict_count;
		program_log_conflict(program, offsets, size, conflicts, conflict_count, false);
		return PROGRAM_MARK_CONFLICT;
	}
	if (conflict_count)
	{
		++program->instruction_conflict_count;
		program_log_conflict(program, offsets, size, conflicts, conflict_count, true);
		if (flags & PROGRAM_INSTRUCTION_EXECUTED)
		{
			u32 executed_conflicts[3] = {};
			u32 executed_conflict_count = 0;
			for (u32 index = 0; index < conflict_count; ++index)
			{
				if (program->bytes[conflicts[index]].flags & PROGRAM_INSTRUCTION_EXECUTED) {
					executed_conflicts[executed_conflict_count++] = conflicts[index];
				}
			}
			if (executed_conflict_count)
			{
				++program->executed_instruction_conflict_count;
				program_log_executed_conflict(program, cpu_address, offsets, size, executed_conflicts, executed_conflict_count);
			}
		}
	}
	for (u32 index = 0; index < conflict_count; ++index)
		program_clear_instruction(program, conflicts[index]);

	b32 existing = program->bytes[offsets[0]].offset_to_start == 0;
	for (u32 byte_index = 0; byte_index < size; ++byte_index)
	{
		ProgramByte *byte = &program->bytes[offsets[byte_index]];
		if (byte_index && byte->offset_to_start == PROGRAM_BYTE_UNKNOWN)
		{
			u32 bucket = offsets[byte_index] / PROGRAM_BUCKET_SIZE;
			Assert(program->instruction_buckets[bucket] > 0);
			--program->instruction_buckets[bucket];
		}
		byte->offset_to_start = (u8)byte_index;
		byte->flags |= flags;
	}
	if (existing) return PROGRAM_MARK_EXISTING;
	++program->instruction_count;
	++program->revision;
	return PROGRAM_MARK_ADDED;
}

static u32 program_instruction_size(u32 type)
{
	return nes_instruction_desc(type).size;
}

static b32 program_is_relative_branch(u32 type)
{
	switch (type)
	{
		case BPL_REL: case BMI_REL: case BVC_REL: case BVS_REL:
		case BCC_REL: case BCS_REL: case BNE_REL: case BEQ_REL: return true;
	}
	return false;
}

static void program_map_instruction(Debugger *debugger, u16 cpu_address, u32 size, u32 *program_offsets)
{
	NES_DeviceId device = NES_DEVICE_NONE;
	for (u32 byte_index = 0; byte_index < size; ++byte_index)
	{
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, (u16)(cpu_address + byte_index));
		if (!program_storage_offset_from_map(&debugger->program, mapped, &program_offsets[byte_index]) || (byte_index && mapped.device != device)) {
			program_offsets[byte_index] = MAX_VALUE_U32;
		}
		device = mapped.device;
	}
}

static void program_map_traced_instruction(const Program *program,
	NES_TraceEntry trace, u32 size, u32 *program_offsets)
{
	for (u32 byte_index = 0; byte_index < 3; ++byte_index) {
		program_offsets[byte_index] = MAX_VALUE_U32;
	}

	u32 first = 0;
	if (!program_storage_offset_from_map(program, trace.cpu_mapped, &first)) {
		return;
	}

	// All currently supported PRG mappers are contiguous within an 8 KiB
	// window. Do not guess across a mapper boundary because the trace records
	// the historical mapping of the opcode, not the mapper's later state.
	const u32 minimum_mapping_window = KiB(8);
	if ((trace.cpu_mapped.offset & (minimum_mapping_window - 1)) + size >
		minimum_mapping_window) {
		return;
	}

	u32 storage_end = first < program->prg_rom_byte_count ?
		program->prg_rom_byte_count : program->byte_count;
	if (size > storage_end - first) return;
	for (u32 byte_index = 0; byte_index < size; ++byte_index) {
		program_offsets[byte_index] = first + byte_index;
	}
}

typedef struct
{
	u32 invalid_bytes;
	u32 discontinuous_branches;
	u32 bridge_mismatches;
	u32 indent_mismatches;
}
ProgramValidation;

static void program_add_bridge_layout(const Program *program, u32 source_offset, u32 destination_offset, u16 *bridges, u8 *indents)
{
	u32 first = Min(source_offset, destination_offset);
	u32 last = Max(source_offset, destination_offset);
	for (u32 offset = first + 1; offset < last; ++offset)
	{
		if (program->bytes[offset].offset_to_start != 0) {
			continue;
		}
		if (indents[offset] < 16) {
			bridges[offset] |= (u16)(1u << indents[offset]);
		}
		if (indents[offset] < MAX_VALUE_U8) {
			++indents[offset];
		}
	}
}

static ProgramValidation program_validate(Debugger *debugger, u16 *expected_bridges, u8 *expected_indents, u8 *expected_sources)
{
	Program *program = &debugger->program;
	ProgramValidation result = {};
	for (u32 offset = 0; offset < program->byte_count; ++offset)
	{
		const ProgramByte *byte = &program->bytes[offset];
		if (byte->offset_to_start != PROGRAM_BYTE_UNKNOWN && byte->offset_to_start > 2) {
			++result.invalid_bytes;
		} else if (byte->offset_to_start > 0 && byte->offset_to_start <= 2 && (offset < byte->offset_to_start || program->bytes[offset - byte->offset_to_start].offset_to_start != 0)) {
			++result.invalid_bytes;
		}
	}
	ProgramIterator iterator = program_iterator(debugger, 0);
	ProgramInstruction instruction;
	while (program_iterator_next(&iterator, &instruction))
	{
		if (!program_is_relative_branch(instruction.type)) {
			continue;
		}
		u32 source_offset = 0;
		Assert(program_storage_offset_from_map(program, instruction.map_addr, &source_offset));
		if (expected_sources[source_offset]) {
			continue;
		}
		expected_sources[source_offset] = true;
		u16 destination_cpu = (u16)(instruction.cpu_address + 2 + (i8)instruction.data);
		NES_MapAddr destination = nes_emulator_cpu_map(debugger->emulator, destination_cpu);
		i32 cpu_delta = (i16)(destination_cpu - instruction.cpu_address);
		u32 destination_offset = 0;
		if (destination.device != instruction.map_addr.device || !program_storage_offset_from_map(program, destination, &destination_offset) || cpu_delta != (i32)destination_offset - (i32)source_offset) {
			++result.discontinuous_branches;
			continue;
		}
		program_add_bridge_layout(program, source_offset, destination_offset, expected_bridges, expected_indents);
	}
	for (u32 offset = 0; offset < program->byte_count; ++offset)
	{
		const ProgramByte *byte = &program->bytes[offset];
		if (byte->bridges != expected_bridges[offset]) {
			++result.bridge_mismatches;
		}
		if (byte->indent != expected_indents[offset]) {
			++result.indent_mismatches;
		}
	}
	return result;
}

b32 program_dump(Debugger *debugger, const char *path, Arena *scratch)
{
	Program *program = &debugger->program;
	u16 *expected_bridges = arena_push_zero(scratch, program->byte_count * sizeof(*expected_bridges));
	u8 *expected_indents = arena_push_zero(scratch, program->byte_count * sizeof(*expected_indents));
	u8 *expected_sources = arena_push_zero(scratch, program->byte_count * sizeof(*expected_sources));
	ProgramValidation validation = program_validate(debugger, expected_bridges, expected_indents, expected_sources);
	FILE *file = fopen(path, "w");
	if (!file) {
		return false;
	}
	fprintf(file, "PROGRAM ANALYSIS\nPRG ROM bytes: %u\nPRG RAM bytes: %u\ninstructions: %u\nexecuted instructions: %llu\ninstruction conflicts: %llu\nexecuted instruction conflicts: %llu\ndiscontinuous instructions: %llu\nrevision: %llu\nrefinement lap: %llu\nrefinement CPU cursor: $%05X\n\n", program->prg_rom_byte_count, program->prg_ram_byte_count, program->instruction_count, program->executed_instruction_count, program->instruction_conflict_count, program->executed_instruction_conflict_count, program->discontinuous_instruction_count, program->revision, program->refinement_pass_count, program->refinement_cpu_cursor);
	fprintf(file, "VALIDATION\ninvalid bytes: %u\ndiscontinuous branches: %u\nbridge mismatches: %u\nindent mismatches: %u\n\n", validation.invalid_bytes, validation.discontinuous_branches, validation.bridge_mismatches, validation.indent_mismatches);
	fprintf(file, "BUCKETS\nINDEX  DEVICE   RANGE           ROWS\n");
	for (u32 bucket = 0; bucket < program->instruction_bucket_count; ++bucket)
	{
		u32 begin = bucket * PROGRAM_BUCKET_SIZE;
		u32 end = Min(begin + PROGRAM_BUCKET_SIZE, program->byte_count);
		const char *device = begin < program->prg_rom_byte_count ? "PRG ROM" : "PRG RAM";
		u32 device_base = begin < program->prg_rom_byte_count ? 0 : program->prg_rom_byte_count;
		fprintf(file, "%5u  %-7s  $%06X-$%06X  %4u\n", bucket, device, begin - device_base, end - 1 - device_base, program->instruction_buckets[bucket]);
	}
	fprintf(file, "\nCURRENTLY MAPPED CPU INSTRUCTIONS\n");
	fprintf(file, "CPU    DEVICE   OFFSET    FLAGS  BRIDGES  EXPECTED  INDENT  EXPECTED\n");
	for (u32 cpu_offset = 0; cpu_offset < NES_CPU_ADDRESS_SPACE; ++cpu_offset)
	{
		u16 cpu_address = (u16)cpu_offset;
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, cpu_address);
		u32 storage_offset = 0;
		if (!program_storage_offset_from_map(program, mapped, &storage_offset) || program->bytes[storage_offset].offset_to_start != 0) {
			continue;
		}
		const ProgramByte *byte = &program->bytes[storage_offset];
		const char *device = mapped.device == NES_DEVICE_PRG_ROM ? "PRG ROM" : "PRG RAM";
		fprintf(file, "$%04X  %-7s  $%06X  $%02X    $%04X   $%04X     %2u      %2u\n", cpu_address, device, mapped.offset, byte->flags, byte->bridges, expected_bridges[storage_offset], byte->indent, expected_indents[storage_offset]);
	}
	b32 success = !ferror(file);
	success = !fclose(file) && success;
	u32 error_count = validation.invalid_bytes + validation.discontinuous_branches + validation.bridge_mismatches + validation.indent_mismatches;
	if (error_count) {
		LOG_WARN("program validation found %u errors: bytes %u, discontinuous branches %u, bridge masks %u, indents %u", error_count, validation.invalid_bytes, validation.discontinuous_branches, validation.bridge_mismatches, validation.indent_mismatches);
	} else {
		LOG_INFO("program validation passed");
	}
	return success;
}

static void program_clear_next_bridge_layout(Program *program)
{
	for (u32 offset = 0; offset < program->byte_count; ++offset)
	{
		program->bytes[offset].next_bridges = 0;
		program->bytes[offset].next_indent = 0;
		program->bytes[offset].next_branch_seen = false;
	}
}

static void program_publish_bridge_layout(Program *program)
{
	for (u32 offset = 0; offset < program->byte_count; ++offset)
	{
		program->bytes[offset].bridges = program->bytes[offset].next_bridges;
		program->bytes[offset].indent = program->bytes[offset].next_indent;
	}
}

static void program_add_next_bridge(Program *program, u32 source_offset, u32 destination_offset)
{
	u32 first = Min(source_offset, destination_offset);
	u32 last = Max(source_offset, destination_offset);
	for (u32 offset = first + 1; offset < last; ++offset)
	{
		ProgramByte *byte = &program->bytes[offset];
		if (byte->offset_to_start != 0) {
			continue;
		}
		if (byte->next_indent < 16) {
			byte->next_bridges |= (u16)(1u << byte->next_indent);
		}
		if (byte->next_indent < MAX_VALUE_U8) {
			++byte->next_indent;
		}
	}
}

void program_refine(Debugger *debugger, u32 instruction_budget)
{
	Program *program = &debugger->program;
	if (!nes_is_booted(debugger->emulator) || !instruction_budget) {
		return;
	}
	ProgramIterator iterator = program_iterator(debugger, program->refinement_cpu_cursor);
	ProgramInstruction instruction;
	u32 processed = 0;
	while (processed < instruction_budget)
	{
		if (!program_iterator_next(&iterator, &instruction))
		{
			if (program->refinement_pass_count & 1) {
				program_publish_bridge_layout(program);
			}
			++program->refinement_pass_count;
			if (program->refinement_pass_count & 1) {
				program_clear_next_bridge_layout(program);
			}
			iterator = program_iterator(debugger, 0);
			continue;
		}
		if ((program->refinement_pass_count & 1) && program_is_relative_branch(instruction.type))
		{
			u32 source_offset = 0;
			Assert(program_storage_offset_from_map(program, instruction.map_addr, &source_offset));
			ProgramByte *source = &program->bytes[source_offset];
			if (!source->next_branch_seen)
			{
				source->next_branch_seen = true;
				u16 destination_cpu = (u16)(instruction.cpu_address + 2 + (i8)instruction.data);
				NES_MapAddr destination = nes_emulator_cpu_map(debugger->emulator, destination_cpu);
				i32 cpu_delta = (i16)(destination_cpu - instruction.cpu_address);
				u32 destination_offset = 0;
				if (destination.device == instruction.map_addr.device && program_storage_offset_from_map(program, destination, &destination_offset) && cpu_delta == (i32)destination_offset - (i32)source_offset) {
					program_add_next_bridge(program, source_offset, destination_offset);
				}
			}
		}
		u32 size = program_instruction_size(instruction.type);
		u32 program_offsets[3] = {};
		program_map_instruction(debugger, instruction.cpu_address, size, program_offsets);
		if (program_offsets_contiguous(program_offsets, size)) {
			program_mark_instruction(program, program_offsets, size, PROGRAM_INSTRUCTION_STATIC, false, instruction.cpu_address);
		}
		++processed;
	}
	program->refinement_cpu_cursor = iterator.cursor;
}

static void program_push_work(Debugger *debugger, ProgramWork work)
{
	NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, work.address);
	u32 storage_offset = 0;
	if (!program_storage_offset_from_map(&debugger->program, mapped, &storage_offset)) return;
	if (program_instruction_storage_offset(&debugger->program, storage_offset) != MAX_VALUE_U32) return;
	void *destination = arena_push_aligned(&debugger->program_work_arena, sizeof(work), 1);
	memory_copy(destination, &work, sizeof(work));
}

static b32 program_has_work(Debugger *debugger)
{
	Assert(arena_used(&debugger->program_work_arena) % sizeof(ProgramWork) == 0);
	return arena_used(&debugger->program_work_arena) != 0;
}

static ProgramWork program_pop_work(Debugger *debugger)
{
	Assert(program_has_work(debugger));
	return *(ProgramWork *)arena_pop(&debugger->program_work_arena,
		sizeof(ProgramWork));
}

static void program_process_work(Debugger *debugger, ProgramWork work)
{
	for (u32 cursor = work.address; cursor < NES_CPU_ADDRESS_SPACE;)
	{
		u16 address = (u16)cursor;
		NES_MapAddr mapped = nes_emulator_cpu_map(debugger->emulator, address);
		u32 type = nes_emulator_cpu_peek(debugger->emulator, address);
		if (!program_is_analyzed_map(&debugger->program, mapped)) {
			break;
		}

		u32 size = program_instruction_size(type);
		u32 program_offsets[3] = {};
		program_map_instruction(debugger, address, size, program_offsets);
		if (!program_offsets_contiguous(program_offsets, size)) {
			break;
		}
		if (program_mark_instruction(&debugger->program, program_offsets, size, PROGRAM_INSTRUCTION_STATIC, false, address) != PROGRAM_MARK_ADDED) {
			break;
		}

		u16 data = nes_emulator_cpu_peek_word(debugger->emulator, (u16)(address + 1));
		switch (type)
		{
			case JMP_ABS:
			case JSR_ABS:
			{
				program_push_work(debugger, (ProgramWork) { .address = data });
			}
			break;
			case BPL_REL: case BMI_REL: case BVC_REL: case BVS_REL:
			case BCC_REL: case BCS_REL: case BNE_REL: case BEQ_REL:
			{
				u16 target = (u16)(address + 2 + (i8)data);
				if (target != address) {
					program_push_work(debugger, (ProgramWork) { .address = target });
				}
			}
			break;
		}

		switch (type)
		{
			case RTI_IMP:
			case RTS_IMP:
			case JMP_IND:
			case JMP_ABS:
				return;
		}

		cursor += size;
	}
}

static void program_run_work(Debugger *debugger)
{
	while (program_has_work(debugger)) {
		program_process_work(debugger, program_pop_work(debugger));
	}
}

void program_reset(Debugger *debugger)
{
	u64 revision = debugger->program.revision + 1;
	memory_zero(&debugger->program, sizeof(debugger->program));
	debugger->program.revision = revision;
	arena_reset(&debugger->program_work_arena);
	debugger->program.prg_rom_byte_count = debugger->emulator->prg_rom_size;
	debugger->program.prg_ram_byte_count = PROGRAM_PRG_RAM_SIZE;
	debugger->program.byte_count = debugger->program.prg_rom_byte_count + debugger->program.prg_ram_byte_count;
	Assert(debugger->program.prg_rom_byte_count <= PROGRAM_MAX_PRG_SIZE);
	Assert(debugger->program.byte_count <= PROGRAM_MAX_SIZE);
	debugger->program.instruction_bucket_count = (debugger->program.byte_count + PROGRAM_BUCKET_SIZE - 1) / PROGRAM_BUCKET_SIZE;
	for (u32 bucket = 0; bucket < debugger->program.instruction_bucket_count; ++bucket)
	{
		u32 begin = bucket * PROGRAM_BUCKET_SIZE;
		u32 end = Min(begin + PROGRAM_BUCKET_SIZE, debugger->program.byte_count);
		debugger->program.instruction_buckets[bucket] = end - begin;
	}
	for (u32 offset = 0; offset < debugger->program.byte_count; ++offset) {
		debugger->program.bytes[offset].offset_to_start = PROGRAM_BYTE_UNKNOWN;
	}

	// This is a stack, so pushing IRQ, NMI, RESET analyzes RESET first.
	const u16 vector_addresses[] = { 0xFFFE, 0xFFFA, 0xFFFC };
	for (u32 index = 0; index < ArrayCount(vector_addresses); ++index)
	{
		u16 vector = vector_addresses[index];
		program_push_work(debugger, (ProgramWork) {
			.address = nes_emulator_cpu_peek_word(debugger->emulator, vector),
		});
	}
	program_run_work(debugger);

}

void program_observe_execution(Debugger *debugger, NES_TraceEntry trace)
{
	Program *program = &debugger->program;
	++program->executed_instruction_count;
	u32 size = program_instruction_size(trace.cpu_byte);
	u32 program_offsets[3] = {
		MAX_VALUE_U32, MAX_VALUE_U32, MAX_VALUE_U32,
	};
	program_map_traced_instruction(program, trace, size, program_offsets);
	if (program_offsets[0] != MAX_VALUE_U32)
	{
		program_update_cached_byte(program, trace.cpu_mapped,
			program_offsets[0], trace.cpu_byte);
	}
	if (program_offsets_contiguous(program_offsets, size))
	{
		program_mark_instruction(program, program_offsets, size,
			PROGRAM_INSTRUCTION_EXECUTED, true, trace.cpu_address);
	}
}
