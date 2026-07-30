#include "serialize.h"

typedef struct
{
	u8 *memory;
	u64 capacity;
	u64 cursor;
	b32 failed;
}
SerializeStream;

static SerializeStream serialize_stream(ByteSpan span)
{
	return (SerializeStream) { span.data, span.size, 0 };
}

static void *serialize_stream_take(SerializeStream *stream, u64 size)
{
	if (size > stream->capacity - stream->cursor)
	{
		stream->failed = true;
		return 0;
	}
	void *result = stream->memory + stream->cursor;
	stream->cursor += size;
	return result;
}

static void serialize_stream_write(SerializeStream *stream, const void *data, u64 size)
{
	void *destination = serialize_stream_take(stream, size);
	if (destination) memory_copy(destination, data, size);
}

static void serialize_stream_read(SerializeStream *stream, void *data, u64 size)
{
	void *source = serialize_stream_take(stream, size);
	if (source) memory_copy(data, source, size);
	else memory_zero(data, size);
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
	SERIALIZE_VERSION_1    = 1,
	SERIALIZE_VERSION      = 2,
	SERIALIZE_CHUNK_RECORD = 1,
};

STATIC_ASSERT(SERIALIZE_WIRE_COUNT <= 16);
STATIC_ASSERT(sizeof(SerializeChunkHeader) == 12);

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

static const SerializeField *serialize_record_field_from_id(
	const SerializeRecord *record, u32 field_id, u32 *field_index)
{
	for (u32 index = 0; index < record->field_count; ++index)
	{
		const SerializeField *field = &record->fields[index];
		if (field->id != field_id) continue;
		if (field_index) *field_index = index;
		return field;
	}
	return 0;
}

static void serialize_write_record_body(SerializeStream *writer, const SerializeRecordMap *map, const SerializeRecord *record, const u8 *value)
{
	Assert(record->field_count <= 64);
	serialize_stream_write_u16(writer, record->id);
	serialize_stream_write_u16(writer, (u16)serialize_enabled_field_count(record));
	for (u32 field_index = 0; field_index < record->field_count; ++field_index)
	{
		const SerializeField *field = &record->fields[field_index];
		if (!(field->flags & SERIALIZE_FIELD_ENABLED)) continue;
		Assert(field->id && field->id <= MAX_VALUE_U16 >> 4);
		serialize_stream_write_u16(writer, (u16)(field->id << 4 | field->wire_type));
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

static b32 serialize_read_record_body(SerializeStream *reader,
	const SerializeRecordMap *map, const SerializeRecord *expected, u8 *value,
	u16 version)
{
	u16 record_id = serialize_stream_read_u16(reader);
	u32 field_count = serialize_stream_read_u16(reader);
	if (reader->failed || expected->field_count > 64 ||
		field_count > expected->field_count) return false;
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!record || record != expected) return false;
	u64 seen_fields = 0;
	for (u32 serialized_index = 0; serialized_index < field_count; ++serialized_index)
	{
		u32 field_header = serialize_stream_read_u16(reader);
		if (reader->failed) return false;
		SerializeWireType wire_type = (SerializeWireType)(field_header & 15);
		u32 field_key = field_header >> 4;
		u32 field_index = 0;
		const SerializeField *field = 0;
		if (version == SERIALIZE_VERSION_1)
		{
			if (field_key >= record->field_count) return false;
			field_index = field_key;
			field = &record->fields[field_index];
		}
		else
		{
			field = serialize_record_field_from_id(record, field_key,
				&field_index);
			if (!field) return false;
		}
		u64 field_bit = (u64)1 << field_index;
		if (seen_fields & field_bit) return false;
		seen_fields |= field_bit;
		if (!(field->flags & SERIALIZE_FIELD_ENABLED) || wire_type != field->wire_type) return false;
		if (wire_type == SERIALIZE_WIRE_RECORD)
		{
			const SerializeRecord *nested = serialize_record_from_id(map, field->record_id);
			u32 count = serialize_stream_read_u32(reader);
			if (reader->failed || !nested || count != field->count ||
				nested->size * count != field->size) return false;
			for (u32 index = 0; index < count; ++index) {
				if (!serialize_read_record_body(reader, map, nested,
					value + field->offset + index * nested->size,
					version)) return false;
			}
		}
		else
		{
			u32 size = serialize_stream_read_u32(reader);
			if (reader->failed || field->record_id || size != field->size)
				return false;
			serialize_stream_read(reader, value + field->offset, size);
			if (reader->failed) return false;
		}
	}
	u64 required_fields = 0;
	for (u32 index = 0; index < record->field_count; ++index)
	{
		if (record->fields[index].flags & SERIALIZE_FIELD_ENABLED) {
			required_fields |= (u64)1 << index;
		}
	}
	return seen_fields == required_fields;
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
	Assert(!writer.failed);
	Assert(writer.cursor - start <= MAX_VALUE_U32);
	header->size = (u32)(writer.cursor - start);
	return writer.cursor;
}

b32 serialize_read_record(ByteSpan source, const SerializeRecordMap *map, u16 record_id, void *value)
{
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!source.data || source.size < sizeof(SerializeChunkHeader) ||
		!record || !value) return false;
	SerializeStream reader = serialize_stream(source);
	SerializeChunkHeader header = {};
	serialize_stream_read(&reader, &header, sizeof(header));
	if (reader.failed || header.magic != SERIALIZE_MAGIC ||
		(header.version != SERIALIZE_VERSION_1 &&
		 header.version != SERIALIZE_VERSION) ||
		header.type != SERIALIZE_CHUNK_RECORD ||
		header.size != source.size - sizeof(header)) return false;
	SerializeStream body = serialize_stream(byte_span(
		source.data + sizeof(header), header.size));
	b32 success = serialize_read_record_body(&body, map, record,
		value, header.version);
	return success && !body.failed && body.cursor == body.capacity;
}
