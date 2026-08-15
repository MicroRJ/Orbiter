#include "execution_graph.h"
#include "nes/isa.h"

STATIC_ASSERT((EXECUTION_GRAPH_EDGE_CAPACITY & (EXECUTION_GRAPH_EDGE_CAPACITY - 1)) == 0);
STATIC_ASSERT((EXECUTION_GRAPH_HASH_CAPACITY & (EXECUTION_GRAPH_HASH_CAPACITY - 1)) == 0);
STATIC_ASSERT(EXECUTION_GRAPH_EDGE_CAPACITY < MAX_VALUE_U16);
STATIC_ASSERT(sizeof(ExecutionEdge) == 16);

static u64 execution_edge_key(u32 source_offset, u32 destination_offset)
{
	return ((u64)source_offset << 32) | destination_offset;
}

static u32 execution_edge_hash(u64 key)
{
	key ^= key >> 30;
	key *= 0xBF58476D1CE4E5B9ull;
	key ^= key >> 27;
	key *= 0x94D049BB133111EBull;
	key ^= key >> 31;
	return (u32)key & (EXECUTION_GRAPH_HASH_CAPACITY - 1);
}

static b32 execution_storage_offset(u32 prg_rom_size, u32 prg_ram_size, NES_MapAddr mapped, u32 *offset)
{
	if (mapped.device == NES_DEVICE_PRG_ROM && mapped.offset < prg_rom_size)
	{
		*offset = mapped.offset;
		return true;
	}
	if (mapped.device == NES_DEVICE_PRG_RAM && mapped.offset < prg_ram_size)
	{
		*offset = prg_rom_size + mapped.offset;
		return true;
	}
	return false;
}

void execution_graph_reset(ExecutionGraph *graph)
{
	u32 generation = graph->generation + 1;
	memory_zero(graph, sizeof(*graph));
	graph->generation = generation;
}

ExecutionEdge *execution_graph_record(ExecutionGraph *graph, u32 source_offset, u32 destination_offset)
{
	u64 key = execution_edge_key(source_offset, destination_offset);
	u32 slot = execution_edge_hash(key);
	for (u32 probe = 0; probe < EXECUTION_GRAPH_HASH_CAPACITY; ++probe)
	{
		ExecutionEdge *edge = &graph->entries[slot];
		if (!edge->hit_count)
		{
			if (graph->edge_count == EXECUTION_GRAPH_EDGE_CAPACITY)
			{
				++graph->overflow_count;
				return 0;
			}

			u32 edge_index = graph->edge_count++;
			*edge = (ExecutionEdge) {
				.key = key,
				.hit_count = 1,
			};
			graph->used_slots[edge_index] = (u16)slot;
			return edge;
		}

		if (edge->key == key)
		{
			++edge->hit_count;
			return edge;
		}
		slot = (slot + 1) & (EXECUTION_GRAPH_HASH_CAPACITY - 1);
	}

	++graph->overflow_count;
	return 0;
}

void execution_graph_observe_execution(ExecutionGraph *graph, ExecutionPathState *path, u32 prg_rom_size, u32 prg_ram_size, NES_TraceEntry boundary)
{
	Assert(graph);
	Assert(path);

	if (path->valid && nes_instruction_links_to_next(path->cpu_address, path->cpu_byte, boundary.cpu_address))
	{
		u32 source_offset = 0;
		u32 destination_offset = 0;
		if (execution_storage_offset(prg_rom_size, prg_ram_size, path->mapped, &source_offset) &&
			execution_storage_offset(prg_rom_size, prg_ram_size, boundary.cpu_mapped, &destination_offset)) {
			execution_graph_record(graph, source_offset, destination_offset);
		}
	}

	path->mapped = boundary.cpu_mapped;
	path->cpu_address = boundary.cpu_address;
	path->cpu_byte = boundary.cpu_byte;
	path->valid = true;
}

void execution_path_discard(ExecutionPathState *path)
{
	path->valid = false;
}
