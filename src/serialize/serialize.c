#include "serialize.h"

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
	SERIALIZE_CHUNK_HEADER_SIZE = 12,
};

STATIC_ASSERT(SERIALIZE_WIRE_COUNT <= 16);
STATIC_ASSERT(SERIALIZE_CHUNK_HEADER_SIZE == sizeof(u32) + sizeof(u16) + sizeof(u16) + sizeof(u32));

static void serialize_transfer_chunk_header(ByteStream *stream, SerializeChunkHeader *header)
{
	byte_transfer_u32(stream, &header->magic);
	byte_transfer_u16(stream, &header->version);
	byte_transfer_u16(stream, &header->type);
	byte_transfer_u32(stream, &header->size);
}

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

static void serialize_write_record_body(ByteStream *writer, const SerializeRecordMap *map, const SerializeRecord *record, const u8 *value)
{
	Assert(record->field_count <= 64);
	u16 record_id = record->id;
	u16 enabled_field_count = (u16)serialize_enabled_field_count(record);
	byte_transfer_u16(writer, &record_id);
	byte_transfer_u16(writer, &enabled_field_count);
	for (u32 field_index = 0; field_index < record->field_count; ++field_index)
	{
		const SerializeField *field = &record->fields[field_index];
		if (!(field->flags & SERIALIZE_FIELD_ENABLED)) continue;
		Assert(field->id && field->id <= MAX_VALUE_U16 >> 4);
		u16 field_header = (u16)(field->id << 4 | field->wire_type);
		byte_transfer_u16(writer, &field_header);
		if (field->wire_type == SERIALIZE_WIRE_RECORD)
		{
			const SerializeRecord *nested = serialize_record_from_id(map, field->record_id);
			Assert(nested && nested->size * field->count == field->size);
			u32 count = field->count;
			byte_transfer_u32(writer, &count);
			for (u32 index = 0; index < field->count; ++index) {
				serialize_write_record_body(writer, map, nested, value + field->offset + index * nested->size);
			}
		}
		else
		{
			Assert(!field->record_id);
			u32 size = field->size;
			byte_transfer_u32(writer, &size);
			byte_transfer_bytes(writer, byte_span((u8 *)value + field->offset, field->size));
		}
	}
}

static b32 serialize_read_record_body(ByteStream *reader,
	const SerializeRecordMap *map, const SerializeRecord *expected, u8 *value,
	u16 version)
{
	u16 record_id = 0;
	u16 serialized_field_count = 0;
	byte_transfer_u16(reader, &record_id);
	byte_transfer_u16(reader, &serialized_field_count);
	u32 field_count = serialized_field_count;
	if (reader->failed || expected->field_count > 64 ||
		field_count > expected->field_count) return false;
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!record || record != expected) return false;
	u64 seen_fields = 0;
	for (u32 serialized_index = 0; serialized_index < field_count; ++serialized_index)
	{
		u16 field_header = 0;
		byte_transfer_u16(reader, &field_header);
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
			u32 count = 0;
			byte_transfer_u32(reader, &count);
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
			u32 size = 0;
			byte_transfer_u32(reader, &size);
			if (reader->failed || field->record_id || size != field->size)
				return false;
			byte_transfer_bytes(reader, byte_span(value + field->offset, size));
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
	ByteStream writer = byte_stream_writer(destination);
	ByteSpan header_bytes = byte_stream_take(&writer, SERIALIZE_CHUNK_HEADER_SIZE);
	u64 start = writer.cursor;
	serialize_write_record_body(&writer, map, record, value);
	Assert(!writer.failed);
	Assert(writer.cursor - start <= MAX_VALUE_U32);
	SerializeChunkHeader header = {
		.magic = SERIALIZE_MAGIC,
		.version = SERIALIZE_VERSION,
		.type = SERIALIZE_CHUNK_RECORD,
		.size = (u32)(writer.cursor - start),
	};
	ByteStream header_writer = byte_stream_writer(header_bytes);
	serialize_transfer_chunk_header(&header_writer, &header);
	Assert(!header_writer.failed);
	return writer.cursor;
}

b32 serialize_read_record(ByteSpan source, const SerializeRecordMap *map, u16 record_id, void *value)
{
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	if (!source.data || source.size < SERIALIZE_CHUNK_HEADER_SIZE ||
		!record || !value) return false;
	ByteStream reader = byte_stream_reader(source);
	SerializeChunkHeader header = {};
	serialize_transfer_chunk_header(&reader, &header);
	if (reader.failed || header.magic != SERIALIZE_MAGIC ||
		(header.version != SERIALIZE_VERSION_1 &&
		 header.version != SERIALIZE_VERSION) ||
		header.type != SERIALIZE_CHUNK_RECORD ||
		header.size != source.size - SERIALIZE_CHUNK_HEADER_SIZE) return false;
	ByteSpan body_bytes = byte_stream_take(&reader, header.size);
	if (reader.failed) return false;
	ByteStream body = byte_stream_reader(body_bytes);
	b32 success = serialize_read_record_body(&body, map, record,
		value, header.version);
	return success && !body.failed && body.cursor == body.size;
}
