#include "serialize.h"

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

static b32 serialize_skip_record_body(ByteStream *reader, u32 depth);

static b32 serialize_skip_field_value(ByteStream *reader, SerializeWireType wire_type, u32 depth)
{
	if (wire_type >= SERIALIZE_WIRE_COUNT) return false;
	if (wire_type == SERIALIZE_WIRE_RECORD)
	{
		u32 count = 0;
		byte_transfer_u32(reader, &count);
		if (reader->failed || count > byte_stream_remaining(reader) / 4) return false;
		for (u32 index = 0; index < count; ++index) {
			if (!serialize_skip_record_body(reader, depth + 1)) return false;
		}
	}
	else
	{
		u32 size = 0;
		byte_transfer_u32(reader, &size);
		byte_stream_skip(reader, size);
	}
	return !reader->failed;
}

static b32 serialize_skip_record_body(ByteStream *reader, u32 depth)
{
	if (depth >= 64) return false;
	u16 record_id = 0;
	u16 field_count = 0;
	byte_transfer_u16(reader, &record_id);
	byte_transfer_u16(reader, &field_count);
	if (reader->failed || !record_id || field_count > byte_stream_remaining(reader) / 6) return false;
	for (u32 index = 0; index < field_count; ++index)
	{
		u16 field_header = 0;
		byte_transfer_u16(reader, &field_header);
		if (reader->failed || !(field_header >> 4)) return false;
		if (!serialize_skip_field_value(reader, (SerializeWireType)(field_header & 15), depth)) return false;
	}
	return true;
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
	const SerializeRecordMap *map, const SerializeRecord *expected, u8 *value)
{
	u16 record_id = 0;
	u16 serialized_field_count = 0;
	byte_transfer_u16(reader, &record_id);
	byte_transfer_u16(reader, &serialized_field_count);
	u32 field_count = serialized_field_count;
	if (reader->failed || expected->field_count > 64) return false;
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
		const SerializeField *field = serialize_record_field_from_id(record, field_key, &field_index);
		if (!field || !(field->flags & SERIALIZE_FIELD_ENABLED))
		{
			if (!serialize_skip_field_value(reader, wire_type, 0)) return false;
			continue;
		}
		u64 field_bit = (u64)1 << field_index;
		if (seen_fields & field_bit) return false;
		seen_fields |= field_bit;
		if (wire_type != field->wire_type) return false;
		if (wire_type == SERIALIZE_WIRE_RECORD)
		{
			const SerializeRecord *nested = serialize_record_from_id(map, field->record_id);
			u32 count = 0;
			byte_transfer_u32(reader, &count);
			if (reader->failed || !nested || count != field->count ||
				nested->size * count != field->size) return false;
			for (u32 index = 0; index < count; ++index) {
				if (!serialize_read_record_body(reader, map, nested, value + field->offset + index * nested->size)) return false;
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
	return true;
}

void serialize_write_record(ByteStream *writer, const SerializeRecordMap *map, u16 record_id, const void *value)
{
	Assert(writer && writer->mode == BYTE_STREAM_WRITE && !writer->failed && !writer->ended);
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	Assert(record && value);
	serialize_write_record_body(writer, map, record, value);
}

b32 serialize_read_record(ByteStream *reader, const SerializeRecordMap *map, u16 record_id, void *value)
{
	Assert(reader && reader->mode == BYTE_STREAM_READ && !reader->failed && !reader->ended);
	const SerializeRecord *record = serialize_record_from_id(map, record_id);
	Assert(record && value);
	b32 success = serialize_read_record_body(reader, map, record, value);
	if (!success) reader->failed = true;
	return success;
}
