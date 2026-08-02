#ifndef NES_TRACE_H
#define NES_TRACE_H


enum
{
	NES_SCHEDULER_TRACE_CLOCK_SHIFT         =  0,
	NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT   =  16,
	NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT      =  32,
	NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT =  40,
	NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT =  44,
	NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK  = 0xF,
	NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK  = (1 << 20) - 1,
};

STATIC_ASSERT(NES_DEVICE_COUNT     - 1 <= NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK);
STATIC_ASSERT(NES_MAX_PRG_ROM_SIZE - 1 <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);
STATIC_ASSERT(NES_MAX_PRG_RAM_SIZE - 1 <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);

typedef struct
{
	// TODO(RJ) we literally don't even have to store this here!
	u64         scheduler_clock;

	u16         cpu_address;
	NES_MapAddr cpu_mapped;
	u8          cpu_byte;
}
NES_TraceEntry;

typedef struct
{
	u64 bits;
}
NES_PackedTraceEntry;
STATIC_ASSERT(sizeof(NES_PackedTraceEntry) * NES_SCHEDULER_TRACE_CAPACITY_POW2 == KiB(512));
STATIC_ASSERT(sizeof(NES_PackedTraceEntry) == 8);

typedef struct
{
	const NES_PackedTraceEntry *trace;
	u64                            index;
	u64                            scheduler_clock;
}
NES_SchedulerTraceView;

typedef struct
{
	const NES_PackedTraceEntry *entries;
	u32 count;
}
NES_SchedulerTraceSpan;

typedef struct
{
	NES_SchedulerTraceSpan spans[2];
	u64 dropped;
}
NES_SchedulerTraceSpans;


static __forceinline NES_PackedTraceEntry nes_scheduler_trace_pack(NES_TraceEntry boundary)
{
	Assert((u32)boundary.cpu_mapped.device <= NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK);
	Assert(boundary.cpu_mapped.offset <= NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK);
	return (NES_PackedTraceEntry) {
		.bits =
			((boundary.scheduler_clock & MAX_VALUE_U16) << NES_SCHEDULER_TRACE_CLOCK_SHIFT) |
			((u64)boundary.cpu_address << NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT) |
			((u64)boundary.cpu_byte << NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT) |
			((u64)boundary.cpu_mapped.device << NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT) |
			((u64)boundary.cpu_mapped.offset << NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT),
	};
}

static inline u64 nes_scheduler_trace_first_since(NES_SchedulerTraceView view, u64 since)
{
	Assert(since <= view.index);
	u64 oldest = view.index > NES_SCHEDULER_TRACE_CAPACITY_POW2 ? view.index - NES_SCHEDULER_TRACE_CAPACITY_POW2 : 0;
	return Max(since, oldest);
}

static inline u64 nes_scheduler_trace_dropped_since(NES_SchedulerTraceView view, u64 since)
{
	return nes_scheduler_trace_first_since(view, since) - since;
}

static inline NES_SchedulerTraceSpans nes_scheduler_trace_spans_since(NES_SchedulerTraceView view, u64 since)
{
	u64 first = nes_scheduler_trace_first_since(view, since);
	u64 count = view.index - first;
	Assert(count <= NES_SCHEDULER_TRACE_CAPACITY_POW2);
	u32 first_slot = (u32)first & NES_SCHEDULER_TRACE_CAPACITY_MASK;
	u32 first_count = (u32)Min(count, NES_SCHEDULER_TRACE_CAPACITY_POW2 - first_slot);
	return (NES_SchedulerTraceSpans) {
		.spans = {
			{ .entries = view.trace + first_slot, .count = first_count },
			{ .entries = view.trace, .count = (u32)count - first_count },
		},
		.dropped = first - since,
	};
}

// TODO(RJ) we don't need this!? we're over-engineering again!
static __forceinline b32 nes_scheduler_trace_clock_reconstructable_since(NES_SchedulerTraceView view, u64 scheduler_clock)
{
	return scheduler_clock <= view.scheduler_clock && view.scheduler_clock - scheduler_clock <= MAX_VALUE_U16;
}

static __forceinline const NES_PackedTraceEntry *nes_scheduler_trace_entry_at(NES_SchedulerTraceView view, u64 index)
{
	u64 oldest = view.index > NES_SCHEDULER_TRACE_CAPACITY_POW2 ? view.index - NES_SCHEDULER_TRACE_CAPACITY_POW2 : 0;
	Assert(index >= oldest && index < view.index);
	return &view.trace[index & NES_SCHEDULER_TRACE_CAPACITY_MASK];
}

static __forceinline NES_TraceEntry nes_scheduler_trace_decode(NES_SchedulerTraceView view, NES_PackedTraceEntry entry)
{
	u64 scheduler_clock = (view.scheduler_clock & ~(u64)MAX_VALUE_U16) | (u16)(entry.bits >> NES_SCHEDULER_TRACE_CLOCK_SHIFT);
	if (scheduler_clock > view.scheduler_clock)
	{
		Assert(scheduler_clock >= (u64)MAX_VALUE_U16 + 1);
		scheduler_clock -= (u64)MAX_VALUE_U16 + 1;
	}
	return (NES_TraceEntry) {
		.scheduler_clock = scheduler_clock,
		.cpu_address = (u16)(entry.bits >> NES_SCHEDULER_TRACE_CPU_ADDRESS_SHIFT),
		.cpu_mapped = {
			.device = (NES_DeviceId)((entry.bits >> NES_SCHEDULER_TRACE_MAPPED_DEVICE_SHIFT) & NES_SCHEDULER_TRACE_MAPPED_DEVICE_MASK),
			.offset = (u32)((entry.bits >> NES_SCHEDULER_TRACE_MAPPED_OFFSET_SHIFT) & NES_SCHEDULER_TRACE_MAPPED_OFFSET_MASK),
		},
		.cpu_byte = (u8)(entry.bits >> NES_SCHEDULER_TRACE_CPU_BYTE_SHIFT),
	};
}

static __forceinline NES_TraceEntry nes_scheduler_trace_at(NES_SchedulerTraceView view, u64 index)
{
	return nes_scheduler_trace_decode(view, *nes_scheduler_trace_entry_at(view, index));
}



#endif