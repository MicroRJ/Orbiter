#ifndef DEBUGGER_EXECUTION_GRAPH_H
#define DEBUGGER_EXECUTION_GRAPH_H

#include "base.h"
#include "program.h"

enum
{
	EXECUTION_GRAPH_EDGE_CAPACITY = KiB(16),
	EXECUTION_GRAPH_HASH_CAPACITY = EXECUTION_GRAPH_EDGE_CAPACITY,
};

typedef struct
{
	u64 key;
	u64 hit_count;
}
ExecutionEdge;

static inline u32 execution_edge_source_offset(const ExecutionEdge *edge)
{
	return (u32)(edge->key >> 32);
}

static inline u32 execution_edge_destination_offset(const ExecutionEdge *edge)
{
	return (u32)edge->key;
}

typedef struct
{
	ExecutionEdge entries[EXECUTION_GRAPH_HASH_CAPACITY];
	u16 used_slots[EXECUTION_GRAPH_EDGE_CAPACITY];
	u32 edge_count;
	u32 generation;
	u64 overflow_count;
}
ExecutionGraph;

static inline const ExecutionEdge *execution_graph_edge(const ExecutionGraph *graph, u32 edge_index)
{
	Assert(edge_index < graph->edge_count);
	return &graph->entries[graph->used_slots[edge_index]];
}

typedef struct
{
	NES_MapAddr mapped;
	u16 cpu_address;
	u8 cpu_byte;
	b32 valid;
}
ExecutionPathState;

void execution_graph_reset(ExecutionGraph *graph);
ExecutionEdge *execution_graph_record(ExecutionGraph *graph, u32 source_offset, u32 destination_offset);
void execution_graph_observe_execution(ExecutionGraph *graph, ExecutionPathState *path, const Program *program, NES_TraceEntry boundary);
void execution_path_discard(ExecutionPathState *path);

#endif
