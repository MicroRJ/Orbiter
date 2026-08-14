#include "base.h"

static u32 failures;

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); failures ++; } } while (0)

static u8 hex_digit_value(char digit)
{
	if (digit >= '0' && digit <= '9') return (u8)(digit - '0');
	if (digit >= 'a' && digit <= 'f') return (u8)(digit - 'a' + 10);
	if (digit >= 'A' && digit <= 'F') return (u8)(digit - 'A' + 10);
	Assert(false);
	return 0;
}

static Hash256 hash_from_hex(const char *hex)
{
	Assert(strlen(hex) == 64);
	Hash256 hash;
	for (u32 index = 0; index < ArrayCount(hash.bytes); index ++) hash.bytes[index] = (u8)(hex_digit_value(hex[index * 2]) << 4 | hex_digit_value(hex[index * 2 + 1]));
	return hash;
}

static void check_vector(ByteSpan input, const char *expected_hex)
{
	Hash256 expected = hash_from_hex(expected_hex);
	CHECK(hash256_match(sha256(input), expected));
}

static void test_standard_vectors(void)
{
	check_vector((ByteSpan) {}, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	check_vector(byte_span("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	char nist[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	check_vector(byte_span(nist, sizeof(nist) - 1), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	u8 block[1000];
	memory_fill(block, 'a', sizeof(block));
	SHA256_Context context;
	sha256_init(&context);
	for (u32 index = 0; index < 1000; index ++) sha256_update(&context, byte_span(block, sizeof(block)));
	Hash256 expected = hash_from_hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	CHECK(hash256_match(sha256_final(&context), expected));
}

static void test_streaming(void)
{
	SHA256_Context context;
	sha256_init(&context);
	sha256_update(&context, byte_span("a", 1));
	sha256_update(&context, (ByteSpan) {});
	sha256_update(&context, byte_span("b", 1));
	sha256_update(&context, byte_span("c", 1));
	CHECK(hash256_match(sha256_final(&context), sha256(byte_span("abc", 3))));

	u8 input[65];
	for (u32 index = 0; index < ArrayCount(input); index ++) input[index] = (u8)(index * 37 + 11);
	u32 sizes[] = { 55, 56, 63, 64, 65 };
	for (u32 size_index = 0; size_index < ArrayCount(sizes); size_index ++)
	{
		u32 size = sizes[size_index];
		sha256_init(&context);
		u32 offset = 0;
		while (offset < size)
		{
			u32 chunk_size = Min(size - offset, offset % 9 + 1);
			sha256_update(&context, byte_span(input + offset, chunk_size));
			offset += chunk_size;
		}
		CHECK(hash256_match(sha256_final(&context), sha256(byte_span(input, size))));
	}
}

static void test_hash_helpers(void)
{
	Hash256 zero = {};
	Hash256 one = { .bytes = { 1 } };
	CHECK(hash256_is_zero(zero));
	CHECK(!hash256_is_zero(one));
	CHECK(hash256_match(one, one));
	CHECK(!hash256_match(zero, one));
}

int main(void)
{
	test_standard_vectors();
	test_streaming();
	test_hash_helpers();
	if (failures)
	{
		fprintf(stderr, "SHA-256 tests failed: %u\n", failures);
		return 1;
	}
	printf("SHA-256 tests passed\n");
	return 0;
}
