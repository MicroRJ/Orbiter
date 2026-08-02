#include "base.h"

int main(void)
{
	u8 expected[] = {
		0xAB,
		0x34, 0x12,
		0xEF, 0xCD, 0xAB, 0x89,
		0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
		0xDE, 0xAD, 0xBE,
	};
	u8 encoded[sizeof(expected)] = {};
	u8 value_u8 = 0xAB;
	u16 value_u16 = 0x1234;
	u32 value_u32 = 0x89ABCDEF;
	u64 value_u64 = 0x0123456789ABCDEF;
	u8 raw[] = { 0xDE, 0xAD, 0xBE };
	ByteStream writer = byte_stream_writer(byte_span(encoded, sizeof(encoded)));
	byte_transfer_u8(&writer, &value_u8);
	byte_transfer_u16(&writer, &value_u16);
	byte_transfer_u32(&writer, &value_u32);
	byte_transfer_u64(&writer, &value_u64);
	byte_transfer_bytes(&writer, byte_span(raw, sizeof(raw)));
	Assert(!writer.failed && writer.cursor == sizeof(encoded));
	Assert(memory_match(encoded, expected, sizeof(expected)));

	u8 overflow = 0x55;
	byte_transfer_u8(&writer, &overflow);
	Assert(writer.failed && writer.cursor == sizeof(encoded));
	overflow = 0x66;
	byte_transfer_u8(&writer, &overflow);
	Assert(writer.failed && writer.cursor == sizeof(encoded));
	Assert(memory_match(encoded, expected, sizeof(expected)));

	value_u8 = 0;
	value_u16 = 0;
	value_u32 = 0;
	value_u64 = 0;
	memory_zero(raw, sizeof(raw));
	ByteStream reader = byte_stream_reader(byte_span(expected, sizeof(expected)));
	byte_transfer_u8(&reader, &value_u8);
	byte_transfer_u16(&reader, &value_u16);
	byte_transfer_u32(&reader, &value_u32);
	byte_transfer_u64(&reader, &value_u64);
	byte_transfer_bytes(&reader, byte_span(raw, sizeof(raw)));
	Assert(!reader.failed && reader.cursor == sizeof(expected));
	Assert(value_u8 == 0xAB && value_u16 == 0x1234 && value_u32 == 0x89ABCDEF && value_u64 == 0x0123456789ABCDEF);
	Assert(memory_match(raw, expected + sizeof(expected) - sizeof(raw), sizeof(raw)));

	overflow = 0x77;
	byte_transfer_u8(&reader, &overflow);
	Assert(reader.failed && reader.cursor == sizeof(expected) && overflow == 0);
	overflow = 0x88;
	byte_transfer_u8(&reader, &overflow);
	Assert(reader.failed && reader.cursor == sizeof(expected) && overflow == 0);

	ByteStream malformed = byte_stream_reader(byte_span(expected, sizeof(expected)));
	malformed.cursor = malformed.size + 1;
	overflow = 0x99;
	byte_transfer_u8(&malformed, &overflow);
	Assert(malformed.failed && malformed.cursor == sizeof(expected) + 1 && overflow == 0);

	ByteStream empty = byte_stream_reader((ByteSpan) {});
	byte_transfer_bytes(&empty, (ByteSpan) {});
	Assert(!empty.failed && !empty.cursor);
	return 0;
}
