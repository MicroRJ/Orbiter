#include "nes_process.h"
#include "nes/isa.h"
#include "program.h"

static b32 program_storage_offset(const Program *program, NES_MapAddr mapped, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < program->prg_rom_byte_count)
	{
		*offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < program->prg_ram_byte_count)
	{
		*offset = program->prg_rom_byte_count + mapped.offset;
		return true;
	}
	return false;
}

static b32 program_is_relative_branch(u16 type)
{
	switch (type)
	{
		case BPL_REL: case BMI_REL: case BVC_REL: case BVS_REL:
		case BCC_REL: case BCS_REL: case BNE_REL: case BEQ_REL: return true;
	}
	return false;
}

static b32 program_mapped_instruction(Program *program, NES_Emulator *emulator, u32 cpu_offset, NES_MapAddr *mapped, u32 *storage_offset)
{
	if (cpu_offset >= NES_CPU_ADDRESS_SPACE) return false;
	*mapped = nes_emulator_cpu_map(emulator, (u16)cpu_offset);
	return program_storage_offset(program, *mapped, storage_offset);
}

static b32 program_crosses_known_start(Program *program, NES_Emulator *emulator, const u8 *evidence, u32 cpu_offset, u32 size)
{
	for (u32 byte_index = 1; byte_index < size; ++byte_index)
	{
		NES_MapAddr mapped;
		u32 storage_offset = 0;
		if (!program_mapped_instruction(program, emulator, cpu_offset + byte_index, &mapped, &storage_offset)) continue;
		if (evidence[storage_offset]) return true;
	}
	return false;
}

static void program_add_bridge(Program *program, u32 source_index, u32 destination_index)
{
	u32 first = Min(source_index, destination_index);
	u32 last = Max(source_index, destination_index);
	for (u32 row_index = first + 1; row_index < last; ++row_index)
	{
		ProgramInstruction *row = &program->rows[row_index];
		if (row->indent < 16)
		{
			row->bridges |= (u16)(1u << row->indent);
			++row->indent;
		}
	}
}

static b32 program_find_exact_cpu_address(const Program *program, u16 cpu_address, u32 *instruction_index)
{
	u32 first = 0;
	u32 count = program->row_count;
	while (count)
	{
		u32 step = count / 2;
		u32 index = first + step;
		u16 candidate = program->rows[index].cpu_address;
		if (candidate < cpu_address)
		{
			first = index + 1;
			count -= step + 1;
		}
		else count = step;
	}
	if (first >= program->row_count || program->rows[first].cpu_address != cpu_address) return false;
	*instruction_index = first;
	return true;
}

// TODO(RJ) eventually I want to introduce arbitrary connecting lines.
static void program_build_bridges(Program *program)
{
	// Keep the listing evidence-driven: bridges connect confirmed rows, but do not create new boundaries.
	PROF_BLOCK("program bridges")
	for (u32 source_index = 0; source_index < program->row_count; ++source_index)
	{
		ProgramInstruction *source = &program->rows[source_index];
		if (source->status != PROGRAM_ROW_INSTRUCTION || !program_is_relative_branch(source->type)) continue;
		u16 destination_cpu = (u16)(source->cpu_address + 2 + (i8)source->data);
		u32 destination_index = 0;
		if (program_find_exact_cpu_address(program, destination_cpu, &destination_index) && program->rows[destination_index].status == PROGRAM_ROW_INSTRUCTION) {
			program_add_bridge(program, source_index, destination_index);
		}
	}
}

void program_invalidate(Program *program)
{
	program->listing_dirty = true;
}

void program_rebuild(Program *program, NES_Emulator *emulator, const u8 *evidence)
{
	if (!program->listing_dirty) return;
	PROF_BLOCK("program listing")
	{
		program->row_count = 0;
		if (nes_emulator_ready_to_run(emulator))
		{
			program->cpu_pc = emulator->cpu.PC;
			program->cpu_pc_mapping = nes_emulator_cpu_map(emulator, program->cpu_pc);

			for (u32 cpu_offset = 0; cpu_offset < NES_CPU_ADDRESS_SPACE;)
			{
				NES_MapAddr mapped;
				u32 storage_offset = 0;
				if (!program_mapped_instruction(program, emulator, cpu_offset, &mapped, &storage_offset))
				{
					++cpu_offset;
					continue;
				}

				u16 cpu_address = (u16)cpu_offset;
				u16 type = nes_emulator_cpu_peek(emulator, cpu_address);
				u32 size = nes_instruction_desc(type).size;
				Assert(size >= 1 && size <= 3);
				u16 data = size >= 2 ? nes_emulator_cpu_peek(emulator, (u16)(cpu_address + 1)) : 0;
				if (size == 3) data |= (u16)nes_emulator_cpu_peek(emulator, (u16)(cpu_address + 2)) << 8;

				u8 flags = evidence[storage_offset];
				b32 conflict = size > 1 && program_crosses_known_start(program, emulator, evidence, cpu_offset, size);
				u32 advance = conflict ? 1 : size;
				ProgramRowStatus status = conflict ? PROGRAM_ROW_ERROR : flags ? PROGRAM_ROW_INSTRUCTION : PROGRAM_ROW_GUESS;

				Assert(program->row_count < ArrayCount(program->rows));
				program->rows[program->row_count++] = (ProgramInstruction) {
					.map_addr = mapped,
					.cpu_address = cpu_address,
					.type = type,
					.data = data,
					.flags = flags,
					.status = status,
				};
				cpu_offset += advance;
			}

			program_build_bridges(program);
		}
		program->listing_dirty = false;
	}
}

b32 program_index_from_cpu_address(const Program *program, u16 cpu_address, u32 *instruction_index)
{
	Assert(program);
	Assert(instruction_index);
	return program_find_exact_cpu_address(program, cpu_address, instruction_index);
}

void program_reset(Program *program, u32 prg_rom_byte_count, u32 prg_ram_byte_count)
{
	memory_zero(program, sizeof(*program));
	program->prg_rom_byte_count = prg_rom_byte_count;
	program->prg_ram_byte_count = prg_ram_byte_count;
	Assert(program->prg_rom_byte_count <= NES_MAX_PRG_ROM_SIZE);
	Assert(program->prg_rom_byte_count + program->prg_ram_byte_count <= PROGRAM_MAX_SIZE);
	program_invalidate(program);
}

b32 program_dump(const Program *program, const char *path)
{
	FILE *file = fopen(path, "w");
	if (!file) return false;
	fprintf(file, "PROGRAM LISTING\nPRG ROM bytes: %u\nPRG RAM bytes: %u\nrows: %u\n\n", program->prg_rom_byte_count, program->prg_ram_byte_count, program->row_count);
	fprintf(file, "INDEX  CPU    DEVICE   OFFSET    FLAGS  TYPE  DATA  STATUS  BRIDGES  INDENT\n");
	for (u32 index = 0; index < program->row_count; ++index)
	{
		const ProgramInstruction *row = &program->rows[index];
		const char *device = row->map_addr.device == NES_DEVICE_PRG_ROM ? "PRG ROM" : "PRG RAM";
		fprintf(file, "%5u  $%04X  %-7s  $%06X  $%02X    $%02X  $%04X  %6u  $%04X   %3d\n", index, row->cpu_address, device, row->map_addr.offset, row->flags, row->type, row->data, row->status, row->bridges, row->indent);
	}
	b32 success = !ferror(file);
	return !fclose(file) && success;
}
