static const u32 sha256_constants[64] =
{
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};

static u32 sha256_rotate_right(u32 value, u32 shift)
{
	return value >> shift | value << (32 - shift);
}

static u32 sha256_read_u32(const u8 *data)
{
	return (u32)data[0] << 24 | (u32)data[1] << 16 | (u32)data[2] << 8 | data[3];
}

static void sha256_process_block(SHA256_Context *context, const u8 *block)
{
	u32 words[64];
	for (u32 index = 0; index < 16; index ++) words[index] = sha256_read_u32(block + index * 4);
	for (u32 index = 16; index < ArrayCount(words); index ++)
	{
		u32 s0 = sha256_rotate_right(words[index - 15], 7) ^ sha256_rotate_right(words[index - 15], 18) ^ words[index - 15] >> 3;
		u32 s1 = sha256_rotate_right(words[index - 2], 17) ^ sha256_rotate_right(words[index - 2], 19) ^ words[index - 2] >> 10;
		words[index] = words[index - 16] + s0 + words[index - 7] + s1;
	}

	u32 a = context->state[0];
	u32 b = context->state[1];
	u32 c = context->state[2];
	u32 d = context->state[3];
	u32 e = context->state[4];
	u32 f = context->state[5];
	u32 g = context->state[6];
	u32 h = context->state[7];
	for (u32 index = 0; index < ArrayCount(words); index ++)
	{
		u32 s1 = sha256_rotate_right(e, 6) ^ sha256_rotate_right(e, 11) ^ sha256_rotate_right(e, 25);
		u32 choice = (e & f) ^ (~e & g);
		u32 temporary1 = h + s1 + choice + sha256_constants[index] + words[index];
		u32 s0 = sha256_rotate_right(a, 2) ^ sha256_rotate_right(a, 13) ^ sha256_rotate_right(a, 22);
		u32 majority = (a & b) ^ (a & c) ^ (b & c);
		u32 temporary2 = s0 + majority;
		h = g;
		g = f;
		f = e;
		e = d + temporary1;
		d = c;
		c = b;
		b = a;
		a = temporary1 + temporary2;
	}
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;
}

b32 hash256_match(Hash256 left, Hash256 right)
{
	return memory_match(left.bytes, right.bytes, sizeof(left.bytes));
}

b32 hash256_is_zero(Hash256 hash)
{
	Hash256 zero = {};
	return hash256_match(hash, zero);
}

void sha256_init(SHA256_Context *context)
{
	Assert(context);
	*context = (SHA256_Context) {
		.state = {
			0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
			0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
		},
	};
}

void sha256_update(SHA256_Context *context, ByteSpan data)
{
	Assert(context);
	Assert(data.data || !data.size);
	Assert(data.size <= ((~(u64)0) >> 3) - context->byte_count);
	context->byte_count += data.size;
	if (!data.size) return;
	const u8 *source = data.data;
	if (context->block_size)
	{
		u32 available = sizeof(context->block) - context->block_size;
		u32 transfer = (u32)Min(data.size, available);
		if (transfer) memory_copy(context->block + context->block_size, source, transfer);
		context->block_size += transfer;
		source += transfer;
		data.size -= transfer;
		if (context->block_size == sizeof(context->block))
		{
			sha256_process_block(context, context->block);
			context->block_size = 0;
		}
	}
	while (data.size >= sizeof(context->block))
	{
		sha256_process_block(context, source);
		source += sizeof(context->block);
		data.size -= sizeof(context->block);
	}
	if (data.size)
	{
		memory_copy(context->block, source, data.size);
		context->block_size = (u32)data.size;
	}
}

Hash256 sha256_final(SHA256_Context *context)
{
	Assert(context);
	u64 bit_size = context->byte_count << 3;
	u32 index = context->block_size;
	context->block[index ++] = 0x80;
	if (index > 56)
	{
		memory_zero(context->block + index, sizeof(context->block) - index);
		sha256_process_block(context, context->block);
		index = 0;
	}
	memory_zero(context->block + index, 56 - index);
	for (u32 byte = 0; byte < 8; byte ++) context->block[56 + byte] = (u8)(bit_size >> (56 - byte * 8));
	sha256_process_block(context, context->block);

	Hash256 hash;
	for (u32 word = 0; word < ArrayCount(context->state); word ++)
	{
		for (u32 byte = 0; byte < 4; byte ++) hash.bytes[word * 4 + byte] = (u8)(context->state[word] >> (24 - byte * 8));
	}
	*context = (SHA256_Context) {};
	return hash;
}

Hash256 sha256(ByteSpan data)
{
	SHA256_Context context;
	sha256_init(&context);
	sha256_update(&context, data);
	return sha256_final(&context);
}
