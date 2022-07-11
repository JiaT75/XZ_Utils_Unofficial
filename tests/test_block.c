///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_block.c
/// \brief      Tests functions from src/liblzma/api/lzma/block.h
///
/// \todo       Uncomment lines 295, 722, and the 
///             test_lzma_block_encoder_sync_flush_unsupported function when
///             LZMA_SYNC_FLUSH is supported in the block encoder
///             Also, a few tests are missing to reach 100% code coverage
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common/index.h"
#include "common/block_encoder.h"
#include "lzma/lzma2_encoder.h"
#include "tests.h"

// Used across multiple tests as the compressed size of a block
#define COMPRESSED_SIZE 4096
// Used across multiple tests as the chunk size when encoding or decoding
#define CHUNK_SIZE 1024

static lzma_test_data* test_data;

static lzma_options_lzma opt_lzma;

static lzma_filter filters[2] = {
	{
		.id = LZMA_FILTER_LZMA2,
		.options = &opt_lzma
	},
	{
		.id = LZMA_VLI_UNKNOWN,
		.options = NULL
	}
};


static void
test_lzma_block_compressed_size(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC32,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters
	};

	const lzma_vli unpadded_size = 100;

	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Test when unpadded_size <= header size + check size
	assert_lzma_ret(lzma_block_compressed_size(&block, 0),
			LZMA_DATA_ERROR);
	assert_lzma_ret(lzma_block_compressed_size(&block, block.header_size +
			lzma_check_size(block.check)), LZMA_DATA_ERROR);

	// Test when block options are invalid
	// Bad version
	block.version = 2;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);
	block.version = 1;

	// Header size too big
	uint32_t correct_header_size = block.header_size;
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);

	// Header size too small
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);

	// Header size not aligned to 4 bytes
	block.header_size = correct_header_size + 1;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);
	block.header_size--;

	// Compressed size invalid
	block.compressed_size = 0;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Check invalid type
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_PROG_ERROR);
	block.check = LZMA_CHECK_CRC32;

	// Test compressed_size != calculated compressed_size
	block.compressed_size = 2 * unpadded_size;
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_DATA_ERROR);
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Test expected result
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_OK);
	assert_uint_eq(block.compressed_size, unpadded_size -
			(block.header_size +
			lzma_check_size(block.check)));
	// Should be able to call lzma_block_compressed_size again without
	// error since compressed_size == calculated compressed_size
	assert_lzma_ret(lzma_block_compressed_size(&block, unpadded_size),
			LZMA_OK);
}


static void
test_lzma_block_unpadded_size(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC32,
		.compressed_size = COMPRESSED_SIZE,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters
	};

	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Test basic passing case
	lzma_vli expected_unpadded_size = block.header_size +
			block.compressed_size +
			lzma_check_size(block.check);
	assert_uint_eq(lzma_block_unpadded_size(&block),
			expected_unpadded_size);

	// Test if compressed_size is unset
	block.compressed_size = LZMA_VLI_UNKNOWN;
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);
	assert_uint_eq(lzma_block_unpadded_size(&block),
			LZMA_VLI_UNKNOWN);
	block.compressed_size = COMPRESSED_SIZE;
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Test when block options are invalid (return value == 0)
	// Invalid version
	block.version = 2;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);
	block.version = 1;

	// Invalid header size
	block.header_size--;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Invalid compressed size
	block.compressed_size = 0;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);

	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);
	block.compressed_size = COMPRESSED_SIZE;

	// Invalid check value
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_uint_eq(lzma_block_unpadded_size(&block), 0);

	// NULL block
	assert_uint_eq(lzma_block_unpadded_size(NULL), 0);
}


static void
test_lzma_block_total_size(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC32,
		.compressed_size = COMPRESSED_SIZE,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters
	};

	// Test basic passing case
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);
	lzma_vli expected_total_size = block.header_size +
			block.compressed_size +
			lzma_check_size(block.check);
	expected_total_size = vli_ceil4(expected_total_size);
	assert_uint_eq(expected_total_size, lzma_block_total_size(&block));

	// Test if compressed_size is unset
	block.compressed_size = LZMA_VLI_UNKNOWN;
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);
	assert_uint_eq(lzma_block_total_size(&block), LZMA_VLI_UNKNOWN);
	block.compressed_size = COMPRESSED_SIZE;
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Test when block options are invalid
	// Invalid version
	block.version = 2;
	assert_uint_eq(lzma_block_total_size(&block), 0);
	block.version = 1;

	// Invalid header size
	block.header_size--;
	assert_uint_eq(lzma_block_total_size(&block), 0);

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_uint_eq(lzma_block_total_size(&block), 0);

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_uint_eq(lzma_block_total_size(&block), 0);
	assert_lzma_ret(lzma_block_header_size(&block), LZMA_OK);

	// Invalid compressed size
	block.compressed_size = 0;
	assert_uint_eq(lzma_block_total_size(&block), 0);

	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_uint_eq(lzma_block_total_size(&block), 0);
	block.compressed_size = COMPRESSED_SIZE;

	// Invalid check value
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_uint_eq(lzma_block_total_size(&block), 0);

	// NULL block
	assert_uint_eq(lzma_block_total_size(NULL), 0);
}


static void
test_lzma_block_encoder(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters,
		.header_size = 0
	};

	// Test with NULL stream
	assert_lzma_ret(lzma_block_encoder(NULL, &block), LZMA_PROG_ERROR);
	lzma_stream strm = LZMA_STREAM_INIT;

	// Test with NULL block
	assert_lzma_ret(lzma_block_encoder(&strm, NULL), LZMA_PROG_ERROR);

	// Test with invalid version
	block.version = 2;
	assert_lzma_ret(lzma_block_encoder(&strm, &block), LZMA_OPTIONS_ERROR);
	block.version = 1;

	// Test with invalid check
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_block_encoder(&strm, &block), LZMA_PROG_ERROR);
	block.check = 9;
	assert_lzma_ret(lzma_block_encoder(&strm, &block),
			LZMA_UNSUPPORTED_CHECK);
	block.check = LZMA_CHECK_CRC64;

	// Test with no filters
	block.filters = NULL;
	assert_lzma_ret(lzma_block_encoder(&strm, &block), LZMA_PROG_ERROR);
	block.filters = filters;

	// Test encoding a block with LZMA_RUN
	assert_lzma_ret(lzma_block_encoder(&strm, &block), LZMA_OK);
	uint8_t out[CHUNK_SIZE * 2];

	// If the decoded size is not at least 2 * CHUNK_SIZE
	// then skip the remainder of this test
	if (test_data->decoded_size < CHUNK_SIZE * 2)
		assert_skip("Test data decoded file too small");

	strm.avail_in = CHUNK_SIZE;
	strm.avail_out = CHUNK_SIZE;
	strm.next_in = test_data->decoded;
	strm.next_out = out;

	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_OK);
	// Test encoding with LZMA_SYNC_FLUSH
	//assert_lzma_ret(lzma_code(&strm, LZMA_SYNC_FLUSH), LZMA_STREAM_END);

	// Test encoding with LZMA_FINISH
	strm.avail_in = CHUNK_SIZE;
	strm.avail_out = CHUNK_SIZE;
	assert_lzma_ret(lzma_code(&strm, LZMA_FINISH), LZMA_STREAM_END);

	// Compare encoded result with expected output
	uint8_t *expected_data = test_data->encoded;
	// Skip past stream header
	expected_data += LZMA_STREAM_HEADER_SIZE;
	// Skip past block header
	expected_data += lzma_block_header_size_decode(expected_data[0]);
	// Skip past lzma2 header
	expected_data += LZMA2_HEADER_MAX;
	uint8_t *out_data = out + LZMA2_HEADER_MAX;
	// Verify compressed output is equal to precomputed output
	// Only testing the first half since LZMA_FINISH alters the
	// end bytes of the block
	assert_array_eq(out_data, expected_data, strm.total_out / 2);
	lzma_end(&strm);
}


/*
static void
test_lzma_block_encoder_sync_flush_unsupported(void)
{
	if (!lzma_filter_encoder_is_supported(LZMA_FILTER_LZMA1))
		assert_skip("LZMA1 filter support is disabled");

	// Test LZMA_SYNC_FLUSH with filters that do not support it
	// lzma1 does not support LZMA_SYNC_FLUSH
	lzma_options_lzma lzma1_block_ops;
	lzma_lzma_preset(&lzma1_block_ops, LZMA_PRESET_DEFAULT);

	lzma_filter lzma1_block_filters[] = {
		{
			.id = LZMA_FILTER_LZMA1,
			.options = &lzma1_block_ops
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};
	
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = lzma1_block_filters,
		.header_size = 0
	};

	lzma_stream strm = LZMA_STREAM_INIT;
	assert_lzma_ret(lzma_block_encoder(&strm, &block), LZMA_OK);
		
	uint8_t out[CHUNK_SIZE * 2];
	memzero(out, CHUNK_SIZE);

	strm.avail_in = CHUNK_SIZE;
	strm.avail_out = CHUNK_SIZE;
	strm.next_in = test_data->decoded;
	strm.next_out = out;

	// Encode some data
	assert_lzma_ret(lzma_code(&strm, LZMA_RUN), LZMA_OK);
	// Expect error when trying to sync_flush
	assert_lzma_ret(lzma_code(&strm, LZMA_SYNC_FLUSH),
			LZMA_OPTIONS_ERROR);

	lzma_end(&strm);
}
*/


static void
test_lzma_block_decoder(void)
{
	lzma_filter decode_filters[LZMA_FILTERS_MAX + 1];
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = decode_filters,
		.header_size = 0
	};

	// Test with NULL stream
	assert_lzma_ret(lzma_block_decoder(NULL, &block), LZMA_PROG_ERROR);

	// Test with NULL block
	lzma_stream strm = LZMA_STREAM_INIT;
	assert_lzma_ret(lzma_block_decoder(&strm, NULL), LZMA_PROG_ERROR);

	// Test with no filters
	block.filters = NULL;
	assert_lzma_ret(lzma_block_decoder(&strm, NULL), LZMA_PROG_ERROR);
	block.filters = decode_filters;

	// Test with invalid version
	block.version = 2;
	assert_lzma_ret(lzma_block_decoder(&strm, NULL), LZMA_PROG_ERROR);
	block.version = 1;

	// Test with invalid compressed_size
	block.compressed_size = 0;
	assert_lzma_ret(lzma_block_decoder(&strm, NULL), LZMA_PROG_ERROR);
	block.compressed_size = LZMA_VLI_UNKNOWN;

	uint8_t *in_buf = test_data->encoded;
	// Skip past stream header
	in_buf += LZMA_STREAM_HEADER_SIZE;
	// Decode block header
	block.header_size = lzma_block_header_size_decode(in_buf[0]);
	assert_lzma_ret(lzma_block_header_decode(&block, NULL, in_buf),
			LZMA_OK);
	in_buf += block.header_size;

	// Decode text stream
	assert_lzma_ret(lzma_block_decoder(&strm, &block), LZMA_OK);
	uint8_t *out_buf = tuktest_malloc(test_data->decoded_size);

	strm.avail_in = test_data->encoded_size - (in_buf -
			test_data->encoded);
	strm.avail_out = test_data->decoded_size;
	strm.next_in = in_buf;
	strm.next_out = out_buf;
	assert_lzma_ret(lzma_code(&strm, LZMA_FINISH), LZMA_STREAM_END);

	assert_uint_eq(strm.total_out, test_data->decoded_size);
	assert_array_eq(out_buf, test_data->decoded,
			test_data->decoded_size);

	lzma_end(&strm);
}


static void
test_lzma_block_buffer_bound(void)
{
	// Test a few values and check if the result is > input
	for (size_t i = 0; i < 0x10000; i += 0x1500) {
		assert_uint(lzma_block_buffer_bound(i), >, i);
	}

	// Test 0 result with >= COMPRESSED_SIZE_MAX
	assert_uint_eq(lzma_block_buffer_bound(COMPRESSED_SIZE_MAX), 0);
}


static void
test_lzma_block_buffer_encode(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters,
		.header_size = 0
	};

	uint8_t *out_buf = tuktest_malloc(test_data->encoded_size);
	size_t out_pos = 0;

	// Test with NULL block
	assert_lzma_ret(lzma_block_buffer_encode(NULL, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size), LZMA_PROG_ERROR);

	// Test with NULL input
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL, NULL,
			test_data->decoded_size, out_buf, &out_pos,
			test_data->encoded_size), LZMA_PROG_ERROR);

	// Test with NULL output
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, NULL,
			&out_pos, test_data->encoded_size), LZMA_PROG_ERROR);

	// Test with NULL out_pos
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			NULL, test_data->encoded_size), LZMA_PROG_ERROR);

	// Test with outpos > outsize
	out_pos = 1;
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size,
			out_buf, &out_pos, 0), LZMA_PROG_ERROR);
	out_pos = 0;

	// Test with invalid version
	block.version = 2;
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size),
			LZMA_OPTIONS_ERROR);
	block.version = 1;

	// Test with invalid check
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size), LZMA_PROG_ERROR);
	block.check = 9;
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size),
			LZMA_UNSUPPORTED_CHECK);
	block.check = LZMA_CHECK_CRC64;

	// Test with no filters
	block.filters = NULL;
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size), LZMA_PROG_ERROR);
	block.filters = filters;

	// Encode block
	assert_lzma_ret(lzma_block_buffer_encode(&block, NULL,
			test_data->decoded, test_data->decoded_size, out_buf,
			&out_pos, test_data->encoded_size), LZMA_OK);
	// Compare to expected
	// Compare encoded result with expected output
	uint8_t *expected_data = test_data->encoded;
	// Skip past stream header
	expected_data += LZMA_STREAM_HEADER_SIZE;
	// Skip past block header
	expected_data += lzma_block_header_size_decode(expected_data[0]);

	// Skip past block header in out_buf
	uint8_t *out_block = out_buf + lzma_block_header_size_decode(
			out_buf[0]);

	// Compare block data
	assert_array_eq(out_block, expected_data, out_pos -
			lzma_block_header_size_decode(out_buf[0]));
}


static void
test_lzma_block_uncomp_encode(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters,
		.header_size = 0
	};

	size_t out_size = lzma_block_buffer_bound(test_data->decoded_size);
	uint8_t *out_buf = tuktest_malloc(out_size);
	size_t out_pos = 0;

	// Test with NULL block
	assert_lzma_ret(lzma_block_uncomp_encode(NULL, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_PROG_ERROR);

	// Test with NULL input
	assert_lzma_ret(lzma_block_uncomp_encode(&block, NULL,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_PROG_ERROR);

	// Test with NULL output
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, NULL, &out_pos, out_size),
			LZMA_PROG_ERROR);

	// Test with NULL out_pos
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, NULL, out_size),
			LZMA_PROG_ERROR);

	// Test with outpos > outsize
	out_pos = 1;
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos, 0),
			LZMA_PROG_ERROR);
	out_pos = 0;

	// Test with invalid version
	block.version = 2;
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_OPTIONS_ERROR);
	block.version = 1;

	// Test with invalid check
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_PROG_ERROR);
	block.check = 9;
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_UNSUPPORTED_CHECK);
	block.check = LZMA_CHECK_CRC64;

	// Encode block
	assert_lzma_ret(lzma_block_uncomp_encode(&block, test_data->decoded,
			test_data->decoded_size, out_buf, &out_pos,
			out_size), LZMA_OK);
	// Compare to expected
	// Skip past block header and lzma2 header in out_buf
	// lzma2 header length discovered to be 3 after trial and error
	uint8_t *out_block = out_buf + lzma_block_header_size_decode(
			out_buf[0]) + LZMA2_HEADER_UNCOMPRESSED;

	// Compare block data through first lzma2 chunk
	assert_array_eq(out_block, test_data->decoded, test_data->decoded_size);
}


static void
test_lzma_block_buffer_decode(void)
{
	lzma_filter decode_filters[LZMA_FILTERS_MAX + 1];
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC64,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = decode_filters,
		.header_size = 0
	};

	size_t out_pos = 0;
	uint8_t *out_buf = tuktest_malloc(test_data->decoded_size);

	size_t in_pos = 0;
	uint8_t *in_buf = test_data->encoded;
	// Skip past stream header
	in_buf += LZMA_STREAM_HEADER_SIZE;
	// Decode block header
	block.header_size = lzma_block_header_size_decode(in_buf[0]);
	assert_lzma_ret(lzma_block_header_decode(&block, NULL, in_buf),
			LZMA_OK);
	in_buf += block.header_size;
	size_t in_buf_size = test_data->encoded_size - (size_t)
			(in_buf - test_data->encoded);

	// Test with NULL block
	assert_lzma_ret(lzma_block_buffer_decode(NULL, NULL, in_buf,
			&in_pos, in_buf_size, out_buf, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);

	// Test with NULL input
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, NULL,
			&in_pos, in_buf_size, out_buf, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);

	// Test with NULL output
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
			&in_pos, in_buf_size, NULL, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);

	// Test with out_pos > out_size
	out_pos = 1;
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
			&in_pos, in_buf_size, out_buf, &out_pos, 0),
			LZMA_PROG_ERROR);
	out_pos = 0;

	// Test with no filters
	block.filters = NULL;
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
			&in_pos, in_buf_size, out_buf, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);
	block.filters = decode_filters;

	// Test with invalid version
	block.version = 2;
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
			&in_pos, in_buf_size, out_buf, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);
	block.version = 1;

	// Test with invalid compressed_size
	block.compressed_size = 0;
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
			&in_pos, in_buf_size, out_buf, &out_pos,
			test_data->decoded_size), LZMA_PROG_ERROR);
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Test successful block decode
	assert_lzma_ret(lzma_block_buffer_decode(&block, NULL, in_buf,
		&in_pos, in_buf_size, out_buf, &out_pos,
		test_data->decoded_size), LZMA_OK);

	assert_array_eq(out_buf, test_data->decoded, test_data->decoded_size);
}


extern int
main(int argc, char **argv)
{
	tuktest_start(argc, argv);
	if (!lzma_filter_encoder_is_supported(LZMA_FILTER_LZMA2)
			|| !lzma_filter_decoder_is_supported(
			LZMA_FILTER_LZMA2))
		tuktest_early_skip("LZMA2 encoder and/or decoder "
				"is disabled");

	if (lzma_lzma_preset(&opt_lzma, LZMA_PRESET_DEFAULT))
		tuktest_error("lzma_lzma_preset() failed");

	test_data = prepare_lzma_test_data("files/small_text.txt.xz",
			"files/small_text.txt");

	tuktest_run(test_lzma_block_compressed_size);
	tuktest_run(test_lzma_block_unpadded_size);
	tuktest_run(test_lzma_block_total_size);
	tuktest_run(test_lzma_block_encoder);
	//tuktest_run(test_lzma_block_encoder_sync_flush_unsupported);
	tuktest_run(test_lzma_block_decoder);
	tuktest_run(test_lzma_block_buffer_bound);
	tuktest_run(test_lzma_block_buffer_encode);
	tuktest_run(test_lzma_block_uncomp_encode);
	tuktest_run(test_lzma_block_buffer_decode);

	return tuktest_end();
}
