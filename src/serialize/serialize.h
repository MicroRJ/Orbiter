#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "base.h"

typedef enum
{
	SERIALIZE_WIRE_BYTES = 0,
	SERIALIZE_WIRE_RECORD,
	SERIALIZE_WIRE_U8,
	SERIALIZE_WIRE_I8,
	SERIALIZE_WIRE_U16,
	SERIALIZE_WIRE_I16,
	SERIALIZE_WIRE_U32,
	SERIALIZE_WIRE_I32,
	SERIALIZE_WIRE_U64,
	SERIALIZE_WIRE_I64,
	SERIALIZE_WIRE_F32,
	SERIALIZE_WIRE_F64,
	SERIALIZE_WIRE_COUNT,
}
SerializeWireType;

typedef enum
{
	SERIALIZE_FIELD_ENABLED = 1 << 0,
}
SerializeFieldFlags;

typedef struct
{
	const char            *name;
	u32                      id;
	SerializeWireType wire_type;
	u16               record_id;
	u32                  offset;
	u32                    size;
	u32                   count;
	u32                   flags;
}
SerializeField;

typedef struct
{
	const char           *name;
	u16                   id;
	u32                   size;
	const SerializeField *fields;
	u32                   field_count;
}
SerializeRecord;

typedef struct
{
	const SerializeRecord *records;
	u32                    record_count;
}
SerializeRecordMap;

b32 serialize_wire_type_is_integer(SerializeWireType type);
const SerializeRecord *serialize_record_from_id(const SerializeRecordMap *map, u16 record_id);
void serialize_write_record(ByteStream *writer, const SerializeRecordMap *map, u16 record_id, const void *value);
b32 serialize_read_record(ByteStream *reader, const SerializeRecordMap *map, u16 record_id, void *value);

#endif
