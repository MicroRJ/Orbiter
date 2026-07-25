#include "serialize.h"

typedef struct
{
	u8 *memory;
	u64 capacity;
	u64 cursor;
}
SerializeStream;

static SerializeStream serialize_stream(ByteSpan span)
{
	return (SerializeStream) { span.data, span.size, 0 };
}

static void *serialize_stream_take(SerializeStream *stream, u64 size)
{
	Assert(stream->cursor + size <= stream->capacity);
	void *result = stream->memory + stream->cursor;
	stream->cursor += size;
	return result;
}

static void serialize_stream_write(SerializeStream *stream, const void *data, u64 size)
{
	memory_copy(serialize_stream_take(stream, size), data, size);
}

static void serialize_stream_read(SerializeStream *stream, void *data, u64 size)
{
	memory_copy(data, serialize_stream_take(stream, size), size);
}

static void serialize_stream_write_u16(SerializeStream *stream, u16 value)
{
	serialize_stream_write(stream, &value, sizeof(value));
}

static void serialize_stream_write_u32(SerializeStream *stream, u32 value)
{
	serialize_stream_write(stream, &value, sizeof(value));
}

static u16 serialize_stream_read_u16(SerializeStream *stream)
{
	u16 value = 0;
	serialize_stream_read(stream, &value, sizeof(value));
	return value;
}

static u32 serialize_stream_read_u32(SerializeStream *stream)
{
	u32 value = 0;
	serialize_stream_read(stream, &value, sizeof(value));
	return value;
}

typedef struct
{
	u32 magic;
	u16 version;
	u16 type;
	u32 size;
}
SerializeChunkHeader;

enum
{
	SERIALIZE_MAGIC        = 0x31524553,
	SERIALIZE_VERSION      = 1,
	SERIALIZE_CHUNK_RECORD = 1,
};

STATIC_ASSERT(SERIALIZE_WIRE_COUNT <= 16);

b32 serialize_wire_type_is_integer(SerializeWireType type)
{
	return type >= SERIALIZE_WIRE_U8 && type <= SERIALIZE_WIRE_I64;
}

const SerializeRecord *serialize_record_from_id(const SerializeRecordMap *map, u16 record_id)
{
	if (!map || !record_id) return 0;
	for (u32 index = 0; index < map->record_count; ++index)
	{
		const SerializeRecord *record = &map->records[index];
		if (record->id == record_id) return record;
	}
	return 0;
}

static u32 serialize_enabled_field_count(const SerializeRecord *record)
{
	u32 count = 0;
	for (u32 index = 0; index < record->field_count; ++index) {
		count += !!(record->fields[index].flags & SERIALIZE_FIELD_ENABLED);
	}
	return count;
}

static void serialize_write_record_body(SerializeStream *writer, const SerializeRecordMap *map, const SerializeRecord *record, const u8 *value)
{
	serialize_stream_write_u16(writer, record->id);
	serialize_stream_write_u16(writer, (u16)serialize_enabled_field_count(record));
	for (u32 field_index = 0; field_index < record->field_count; ++field_index)
	{
		const SerializeField *field = &record->fields[field_index];
		if (!(field->flags & SERIALIZE_FIELD_ENABLED)) continue;
		Assert(field_index <= MAX_VALUE_U16 >> 4);
		serialize_stream_write_u16(writer, (u16)(field_index << 4 | field->wire_type));
		if (field->wire_type == SERIALIZE_WIRE_RECORD)
		{
			const SerializeRecord *nested = serialize_record_from_id(map, field->record_id);
			Assert(nested && nested->size * field->count == field->size);
			serialize_stream_write_u32(writer, field->count);
			for (u32 index = 0; index < field->count; ++index) {
				serialize_write_record_body(writer, map, nested, value + field->offset + index * nested->size);
			}
		}
		else
		{
			Assert(!field->record_id);
			serialize_stream_write_u32(writer, field->size);
			serialize_stream_write(writer, value + field->offset, field->size);
		}
	}
}

static b32 serialize_read_record_body(SerializeStream *reader, const SerializeRecordMap *map, const SerializeRecord *expected, u8 *value)
{
	u16 record_id = serialize_stream_read_u16(reader);
	u32 field_count = serialize_stream_read_u16(reader);
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!record || record != expected) return false;
	for (u32 serialized_index = 0; serialized_index < field_count; ++serialized_index)
	{
		u32 field_header = serialize_stream_read_u16(reader);
		SerializeWireType wire_type = (SerializeWireType)(field_header & 15);
		u32 field_index = field_header >> 4;
		if (field_index >= record->field_count) return false;
		const SerializeField *field = &record->fields[field_index];
		if (!(field->flags & SERIALIZE_FIELD_ENABLED) || wire_type != field->wire_type) return false;
		if (wire_type == SERIALIZE_WIRE_RECORD)
		{
			const SerializeRecord *nested = serialize_record_from_id(map, field->record_id);
			u32 count = serialize_stream_read_u32(reader);
			if (!nested || count != field->count || nested->size * count != field->size) return false;
			for (u32 index = 0; index < count; ++index) {
				if (!serialize_read_record_body(reader, map, nested, value + field->offset + index * nested->size)) return false;
			}
		}
		else
		{
			u32 size = serialize_stream_read_u32(reader);
			if (field->record_id || size != field->size) return false;
			serialize_stream_read(reader, value + field->offset, size);
		}
	}
	return true;
}

u64 serialize_write_record(ByteSpan destination, const SerializeRecordMap *map, u16 record_id, const void *value)
{
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	Assert(destination.data && record && value);
	SerializeStream writer = serialize_stream(destination);
	SerializeChunkHeader *header = serialize_stream_take(&writer, sizeof(*header));
	header->magic = SERIALIZE_MAGIC;
	header->version = SERIALIZE_VERSION;
	header->type = SERIALIZE_CHUNK_RECORD;
	header->size = 0;
	u64 start = writer.cursor;
	serialize_write_record_body(&writer, map, record, value);
	Assert(writer.cursor - start <= MAX_VALUE_U32);
	header->size = (u32)(writer.cursor - start);
	return writer.cursor;
}

b32 serialize_read_record(ByteSpan source, const SerializeRecordMap *map, u16 record_id, void *value)
{
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!source.data || !record || !value) return false;
	SerializeStream reader = serialize_stream(source);
	SerializeChunkHeader header = {};
	serialize_stream_read(&reader, &header, sizeof(header));
	if (header.magic != SERIALIZE_MAGIC || header.version != SERIALIZE_VERSION || header.type != SERIALIZE_CHUNK_RECORD) return false;
	return serialize_read_record_body(&reader, map, record, value);
}
