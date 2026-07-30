#include "debugger.h"
#include "execution_graph.h"
#include "nes/isa.h"
#include <stdio.h>

static ExecutionGraph graph;
static ExecutionPathState path;
static Program program;

static const ExecutionEdge *find_edge(u32 source_offset, u32 destination_offset)
{
	for (u32 index = 0; index < graph.edge_count; ++index)
	{
		const ExecutionEdge *edge = execution_graph_edge(&graph, index);
		if (execution_edge_source_offset(edge) == source_offset &&
			execution_edge_destination_offset(edge) == destination_offset)
		{
			return edge;
		}
	}
	return 0;
}

static NES_SchedulerBoundary boundary(u16 cpu_address, NES_DeviceId device,
	u32 offset, u8 cpu_byte)
{
	return (NES_SchedulerBoundary) {
		.cpu_address = cpu_address,
		.cpu_mapped = nes_map_addr(device, offset),
		.cpu_byte = cpu_byte,
	};
}

static void test_recording(void)
{
	execution_graph_reset(&graph);
	Assert(graph.generation == 1);

	ExecutionEdge *first = execution_graph_record(&graph, 5, 19);
	Assert(first);
	Assert(first->hit_count == 1);
	Assert(graph.edge_count == 1);
	Assert(execution_edge_source_offset(first) == 5);
	Assert(execution_edge_destination_offset(first) == 19);

	Assert(execution_graph_record(&graph, 5, 19) == first);
	Assert(first->hit_count == 2);
	Assert(graph.edge_count == 1);

	ExecutionEdge *second = execution_graph_record(&graph, 7, 23);
	Assert(second && second != first);
	Assert(graph.edge_count == 2);
	Assert(execution_graph_edge(&graph, 0) == first);
	Assert(execution_graph_edge(&graph, 1) == second);

	execution_graph_reset(&graph);
	Assert(graph.generation == 2);
	Assert(graph.edge_count == 0);
	Assert(graph.overflow_count == 0);
}

static void test_capacity(void)
{
	execution_graph_reset(&graph);
	ExecutionEdge *first = 0;
	for (u32 index = 0; index < EXECUTION_GRAPH_EDGE_CAPACITY; ++index)
	{
		ExecutionEdge *edge = execution_graph_record(&graph, index,
			index ^ 0xA5A5A5A5u);
		Assert(edge);
		if (!index) first = edge;
	}
	Assert(graph.edge_count == EXECUTION_GRAPH_EDGE_CAPACITY);
	Assert(!execution_graph_record(&graph, MAX_VALUE_U32, 0));
	Assert(graph.overflow_count == 1);

	Assert(execution_graph_record(&graph, 0, 0xA5A5A5A5u) == first);
	Assert(first->hit_count == 2);
	Assert(graph.overflow_count == 1);
}

static void test_observation(void)
{
	memory_zero(&program, sizeof(program));
	program.prg_rom_byte_count = KiB(32);
	program.prg_ram_byte_count = KiB(8);
	program.byte_count = program.prg_rom_byte_count +
		program.prg_ram_byte_count;
	execution_graph_reset(&graph);
	execution_path_discard(&path);

	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x8000, NES_DEVICE_PRG_ROM, 5, LDA_IMM));
	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x8002, NES_DEVICE_PRG_ROM, 7, BNE_REL));
	Assert(graph.edge_count == 0);

	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x9000, NES_DEVICE_PRG_ROM, 23, LDA_IMM));
	const ExecutionEdge *branch = find_edge(7, 23);
	Assert(branch && branch->hit_count == 1);

	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x9002, NES_DEVICE_PRG_ROM, 25, JMP_ABS));
	Assert(graph.edge_count == 1);
	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x6000, NES_DEVICE_PRG_RAM, 3, LDA_IMM));
	const ExecutionEdge *rom_to_ram = find_edge(25,
		program.prg_rom_byte_count + 3);
	Assert(rom_to_ram);

	execution_path_discard(&path);
	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x7000, NES_DEVICE_PRG_RAM, 11, JMP_ABS));
	Assert(graph.edge_count == 2);
	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0xA000, NES_DEVICE_PRG_ROM, 31, LDA_IMM));
	const ExecutionEdge *ram_to_rom = find_edge(
		program.prg_rom_byte_count + 11, 31);
	Assert(ram_to_rom);

	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0xA002, NES_DEVICE_PRG_ROM, 33, JMP_ABS));
	execution_graph_observe_execution(&graph, &path, &program,
		boundary(0x0200, NES_DEVICE_WRAM, 0x200, LDA_IMM));
	Assert(graph.edge_count == 3);

	execution_path_discard(&path);
	Assert(!path.valid);
}

static void test_executed_program_marking(void)
{
	Arena arena = arena_create(0, "execution graph debugger test");
	u32 rom_size = 16 + KiB(16) + KiB(8);
	u8 *rom = arena_push_zero(&arena, rom_size);
	rom[0] = 'N';
	rom[1] = 'E';
	rom[2] = 'S';
	rom[3] = 0x1A;
	rom[4] = 1;
	rom[5] = 1;
	rom[16 + 0] = NOP_IMP;
	rom[16 + 1] = JMP_ABS;
	rom[16 + 2] = 0x00;
	rom[16 + 3] = 0x80;
	rom[16 + 0x3FFA] = 0x00;
	rom[16 + 0x3FFB] = 0x80;
	rom[16 + 0x3FFC] = 0x00;
	rom[16 + 0x3FFD] = 0x80;
	rom[16 + 0x3FFE] = 0x00;
	rom[16 + 0x3FFF] = 0x80;

	Debugger *debugger = debugger_create(&arena, 48000);
	Assert(debugger_open_rom(debugger, byte_span(rom, rom_size)));
	NES_MapAddr first = debugger_cpu_map(debugger, 0x8000);
	const Program *observed = debugger_program(debugger);
	u32 first_offset = program_mapped_instruction_offset(observed, first);
	Assert(first_offset != MAX_VALUE_U32);
	Assert(!(observed->bytes[first_offset].flags &
		PROGRAM_INSTRUCTION_EXECUTED));

	Assert(debugger_step(debugger) > 0);
	observed = debugger_program(debugger);
	first_offset = program_mapped_instruction_offset(observed, first);
	Assert(first_offset != MAX_VALUE_U32);
	Assert(observed->bytes[first_offset].flags &
		PROGRAM_INSTRUCTION_EXECUTED);

	debugger_destroy(debugger);
	arena_destroy(&arena);
}

int main(void)
{
	Assert(nes_instruction_is_control_flow(BNE_REL));
	Assert(nes_instruction_is_control_flow(JMP_ABS));
	Assert(!nes_instruction_is_control_flow(LDA_IMM));
	test_recording();
	test_capacity();
	test_observation();
	test_executed_program_marking();
	printf("Orbiter execution graph tests passed\n");
	return 0;
}
