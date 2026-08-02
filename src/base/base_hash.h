#ifndef BASE_HASH_H
#define BASE_HASH_H

typedef struct
{
	u8 bytes[32];
}
Hash256;
STATIC_ASSERT(sizeof(Hash256) == 32);

typedef struct
{
	u32 state[8];
	u64 byte_count;
	u8 block[64];
	u32 block_size;
}
SHA256_Context;

b32 hash256_match(Hash256 left, Hash256 right);
b32 hash256_is_zero(Hash256 hash);

void sha256_init(SHA256_Context *context);
void sha256_update(SHA256_Context *context, ByteSpan data);
// Finalization consumes and clears the context; call sha256_init before reuse.
Hash256 sha256_final(SHA256_Context *context);
Hash256 sha256(ByteSpan data);

#endif
