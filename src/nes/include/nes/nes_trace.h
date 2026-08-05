#ifndef NES_TRACE_H
#define NES_TRACE_H

typedef struct
{
	NES_MapAddr cpu_mapped;
	u16 cpu_address;
	u8 cpu_byte;
}
NES_TraceEntry;

STATIC_ASSERT(sizeof(NES_TraceEntry) == 12);

#endif
