#include "base.h"
#include "serialize.h"

enum
{
	TEST_RECORD_ROOT = 1,
	TEST_RECORD_REMOVED,
	TEST_FIELD_KEPT = 1,
	TEST_FIELD_REMOVED,
	TEST_FIELD_REMOVED_RECORD,
	TEST_FIELD_ADDED,
};

typedef struct { u8 value; } TestRemoved;
typedef struct { u8 kept; u32 removed; TestRemoved removed_record; } TestOld;
typedef struct { u8 kept; u16 added; } TestNew;
typedef struct { u16 kept; } TestIncompatible;

#define TEST_FIELD(id_, type_, field_, wire_type_, record_id_) \
	{ .name = #field_, .id = (id_), .wire_type = (wire_type_), .record_id = (record_id_), .offset = offsetof(type_, field_), .size = sizeof(((type_ *)0)->field_), .count = 1, .flags = SERIALIZE_FIELD_ENABLED }

static const SerializeField test_removed_fields[] =
{
	TEST_FIELD(TEST_FIELD_REMOVED, TestRemoved, value, SERIALIZE_WIRE_U8, 0),
};

static const SerializeField test_old_fields[] =
{
	TEST_FIELD(TEST_FIELD_KEPT,           TestOld, kept,           SERIALIZE_WIRE_U8,     0),
	TEST_FIELD(TEST_FIELD_REMOVED,        TestOld, removed,        SERIALIZE_WIRE_U32,    0),
	TEST_FIELD(TEST_FIELD_REMOVED_RECORD, TestOld, removed_record, SERIALIZE_WIRE_RECORD, TEST_RECORD_REMOVED),
};

static const SerializeField test_new_fields[] =
{
	TEST_FIELD(TEST_FIELD_KEPT,  TestNew, kept,  SERIALIZE_WIRE_U8,  0),
	TEST_FIELD(TEST_FIELD_ADDED, TestNew, added, SERIALIZE_WIRE_U16, 0),
};

static const SerializeField test_incompatible_fields[] =
{
	TEST_FIELD(TEST_FIELD_KEPT, TestIncompatible, kept, SERIALIZE_WIRE_U16, 0),
};

#define TEST_RECORD(id_, type_, fields_) \
	{ .name = #type_, .id = (id_), .size = sizeof(type_), .fields = (fields_), .field_count = ArrayCount(fields_) }

static const SerializeRecord test_old_records[] =
{
	TEST_RECORD(TEST_RECORD_ROOT,    TestOld,     test_old_fields),
	TEST_RECORD(TEST_RECORD_REMOVED, TestRemoved, test_removed_fields),
};

static const SerializeRecord test_new_records[] =
{
	TEST_RECORD(TEST_RECORD_ROOT, TestNew, test_new_fields),
};

static const SerializeRecord test_incompatible_records[] =
{
	TEST_RECORD(TEST_RECORD_ROOT, TestIncompatible, test_incompatible_fields),
};

int main(void)
{
	u8 encoded[256] = {};
	TestOld old_value = { .kept = 0x12, .removed = 0x34567890, .removed_record = { .value = 0xAB } };
	SerializeRecordMap old_map = { .records = test_old_records, .record_count = ArrayCount(test_old_records) };
	SerializeRecordMap new_map = { .records = test_new_records, .record_count = ArrayCount(test_new_records) };
	SerializeRecordMap incompatible_map = { .records = test_incompatible_records, .record_count = ArrayCount(test_incompatible_records) };

	ByteStream writer = byte_stream_writer(byte_span(encoded, sizeof(encoded)));
	serialize_write_record(&writer, &old_map, TEST_RECORD_ROOT, &old_value);
	Assert(!writer.failed);
	u64 encoded_size = writer.cursor;

	TestNew new_value = { .added = 0xBEEF };
	ByteStream reader = byte_stream_reader(byte_span(encoded, encoded_size));
	Assert(serialize_read_record(&reader, &new_map, TEST_RECORD_ROOT, &new_value));
	Assert(!reader.failed && reader.cursor == encoded_size);
	Assert(new_value.kept == old_value.kept);
	Assert(new_value.added == 0xBEEF);

	TestIncompatible incompatible_value = {};
	reader = byte_stream_reader(byte_span(encoded, encoded_size));
	Assert(!serialize_read_record(&reader, &incompatible_map, TEST_RECORD_ROOT, &incompatible_value));
	Assert(reader.failed);

	TestNew written_new = { .kept = 0x67, .added = 0xCAFE };
	writer = byte_stream_writer(byte_span(encoded, sizeof(encoded)));
	serialize_write_record(&writer, &new_map, TEST_RECORD_ROOT, &written_new);
	Assert(!writer.failed);
	encoded_size = writer.cursor;

	TestOld read_old = { .removed = 0x11223344, .removed_record = { .value = 0x55 } };
	reader = byte_stream_reader(byte_span(encoded, encoded_size));
	Assert(serialize_read_record(&reader, &old_map, TEST_RECORD_ROOT, &read_old));
	Assert(!reader.failed && reader.cursor == encoded_size);
	Assert(read_old.kept == written_new.kept);
	Assert(read_old.removed == 0x11223344);
	Assert(read_old.removed_record.value == 0x55);
	return 0;
}
