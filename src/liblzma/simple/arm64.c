///////////////////////////////////////////////////////////////////////////////
//
/// \file       arm64.c
/// \brief      Filter for ARM64 binaries
///
//  Authors:    Lasse Collin
//              Liao Hua
//              Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_private.h"

// 28 bit mask ending in 0xC since the last two bits need to be ignored
#define MAX_DEST_VALUE 0xFFFFFFC
// Op code for the bl instruction in arm64
#define ARM64_BL_OPCODE 0x25

/*
 * In ARM64, there are two main branch instructions.
 * bl - branch and link. Calls a function and stores the return address.
 * b - branch. Jumps to a location, but does not store the return address.
 *
 * After some benchmarking, it is determined that only the bl instruction
 * is beneficial for compression. A majority of the jumps for the b
 * instruction are very small (+/- 0xFF). These are
 * typical for loops and if statements.
 * Encoding them to their absolute address reduces redundancy since
 * many of the small relative jump values are repeated,
 * but very few of the absolute address are.
 *
 * Thus, only the bl instruction will be encoded and decoded.
 * The bl instruction uses 26 bits for it the immediate value and 6
 * bits for the opcode (0x25).
 * The immediate is shifted by 2, then sign extended to calculate
 * the absolute address for a jump.
 *
 * However, in our encoding and decoding, the sign extension is ignored and
 * values are calculated as unsigned integers only.
 * This is to prevent issues with integer overflows so the
 * decoder can know if the original value was +/- in call cases
*/
static size_t
arm64_code(void *simple lzma_attribute((__unused__)),
		uint32_t now_pos, bool is_encoder,
		uint8_t *buffer, size_t size)
{
	size_t i;
	for (i = 0; i + 4 <= size; i += 4) {
		uint8_t opcode = buffer[i+3] >> 2;
		if (opcode == ARM64_BL_OPCODE) {
			// Combine 26 bit immediate into an unsigned value
			uint32_t src = ((uint32_t)(buffer[i + 3]
					& 0x3) << 24) |
					((uint32_t)(buffer[i + 2]) << 16) |
					((uint32_t)(buffer[i + 1]) << 8) |
					(uint32_t)(buffer[i + 0]);

			// If the immediate is 0, then redundency will be
			// lost by trying to encode it
			// Instead, ignore these values, which are common in
			// things like Linux kernel modules
			if(src == 0)
				continue;

			// Adjust immdediate by * 4 as described in
			// ARM64 bl instruction spec
			src <<= 2;

			uint32_t dest;
			uint32_t pc = now_pos + (uint32_t)(i);

			if (is_encoder)
				dest = pc + src;
			else
				dest = src - pc;

			// Since the decoder will also ignore src values
			// of 0, we must ensure nothing is ever encoded
			// to 0. In the case it is, set the value to +/-
			// pc in order to encode / decode properly
			if((dest & MAX_DEST_VALUE) == 0){
				assert((pc & MAX_DEST_VALUE) != 0);
				dest = is_encoder ? pc : 0U - pc;
			}

			// Re-adjust dest by / 4 to re-encode
			dest >>= 2;

			// Set the lower bits of the buffer[i+3]
			// to bits 25 and 26 of the dest value
			// Next, OR in the correct opcode
			buffer[i + 3] = ((dest >> 24) & 0x3) |
					(ARM64_BL_OPCODE << 2);
			buffer[i + 2] = (dest >> 16);
			buffer[i + 1] = (dest >> 8);
			buffer[i + 0] = dest;
		}
	}

	return i;
}


static lzma_ret
arm64_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
		const lzma_filter_info *filters, bool is_encoder)
{
	return lzma_simple_coder_init(next, allocator, filters,
		&arm64_code, 0, 4, 4, is_encoder);
}


extern lzma_ret
lzma_simple_arm64_encoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return arm64_coder_init(next, allocator, filters, true);
}


extern lzma_ret
lzma_simple_arm64_decoder_init(lzma_next_coder *next,
		const lzma_allocator *allocator,
		const lzma_filter_info *filters)
{
	return arm64_coder_init(next, allocator, filters, false);
}
