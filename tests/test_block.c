///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_block.c
/// \brief      Tests functions from src/liblzma/api/lzma/block.h
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "sysdefs.h"
#include "test_utils.h"
#include "test_lzma_filter_utils.h"
#include "src/liblzma/common/block_encoder.h"
#include "src/liblzma/common/index.h"


// Copied from src/liblzma/lzma/lzma2_encoder.h
#define LZMA2_CHUNK_MAX (UINT32_C(1) << 16)
#define LZMA2_HEADER_UNCOMPRESSED 3
#define LZMA2_HEADER_MAX 6

#define BLOCK_SIZE 0x1000

static test_file_data text_data = {
	.compressed_filename = "files/lzma_block/text.xz",
	.plain_filename = "files/lzma_block/text"
};


lzma_filter filters[2] = {
	{
		.id = LZMA_FILTER_LZMA2,
		.options = NULL
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
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	// Test when unpadded_size <= header size + check size
	assert_int_equal(LZMA_DATA_ERROR, lzma_block_compressed_size(
			&block, 0));
	assert_int_equal(LZMA_DATA_ERROR, lzma_block_compressed_size(
			&block, block.header_size +
			lzma_check_size(block.check) - 1));
	assert_int_equal(LZMA_DATA_ERROR, lzma_block_compressed_size(
			&block, block.header_size +
			lzma_check_size(block.check)));

	// Test when block options are invalid
	// Bad version
	block.version = 2;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	block.version = 1;
	// Header size to big
	uint32_t correct_header_size = block.header_size;
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	// Header size to small
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	// Header size not aligned to 4 bytes
	block.header_size = correct_header_size + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	block.header_size--;
	// Compressed size invalid
	block.compressed_size = 0;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	block.compressed_size = LZMA_VLI_UNKNOWN;
	// Check invalid type
	block.check = 100;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_compressed_size(
			&block, 100));
	block.check = LZMA_CHECK_CRC32;

	// Test compressed_size != calculated compressed_size
	block.compressed_size = 200;
	assert_int_equal(LZMA_DATA_ERROR, lzma_block_compressed_size(
			&block, 100));
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Test expected result
	assert_int_equal(LZMA_OK, lzma_block_compressed_size(&block, 100));
	assert_int_equal(100 - (block.header_size + lzma_check_size(
			block.check)), block.compressed_size);
	// Should be able to call lzma_block_compressed_size again without
	// error since compressed_size == calculated compressed_size
	assert_int_equal(LZMA_OK, lzma_block_compressed_size(&block, 100));
}


static void
test_lzma_block_unpadded_size(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC32,
		.compressed_size = 0x1000,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters
	};

	// Test basic passing case
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	lzma_vli expected_unpadded_size = block.header_size +
			block.compressed_size +
			lzma_check_size(block.check);
	assert_ulong_equal(expected_unpadded_size,
			lzma_block_unpadded_size(&block));

	// Test if compressed_size is unset
	block.compressed_size = LZMA_VLI_UNKNOWN;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_ulong_equal(LZMA_VLI_UNKNOWN,
			lzma_block_unpadded_size(&block));
	block.compressed_size = 0x1000;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	// Test when block options are invalid
	// Invalid version
	block.version = 2;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));
	block.version = 1;

	// Invalid header size
	block.header_size--;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	// Invalid compressed size
	block.compressed_size = 0;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));

	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));
	block.compressed_size = 0x1000;

	// Invalid check value
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_ulong_equal(0, lzma_block_unpadded_size(&block));

	// NULL block
	assert_ulong_equal(0, lzma_block_unpadded_size(NULL));
}


// Mostly repeated tests from test_lzma_block_unpadded size
// since the implementation calls lzma_block_unpadded_size to
// do most of the work. These tests are here in case that ever changes
static void
test_lzma_block_total_size(void)
{
	lzma_block block = {
		.version = 1,
		.check = LZMA_CHECK_CRC32,
		.compressed_size = 0x1000,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.ignore_check = false,
		.filters = filters
	};

	// Test basic passing case
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	lzma_vli expected_total_size = block.header_size +
			block.compressed_size +
			lzma_check_size(block.check);
	expected_total_size = vli_ceil4(expected_total_size);
	assert_ulong_equal(expected_total_size,
			lzma_block_total_size(&block));

	// Test if compressed_size is unset
	block.compressed_size = LZMA_VLI_UNKNOWN;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_ulong_equal(LZMA_VLI_UNKNOWN, lzma_block_total_size(&block));
	block.compressed_size = 0x1000;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	// Test when block options are invalid
	// Invalid version
	block.version = 2;
	assert_ulong_equal(0, lzma_block_total_size(&block));
	block.version = 1;

	// Invalid header size
	block.header_size--;
	assert_ulong_equal(0, lzma_block_total_size(&block));

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 1;
	assert_ulong_equal(0, lzma_block_total_size(&block));

	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 1;
	assert_ulong_equal(0, lzma_block_total_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	// Invalid compressed size
	block.compressed_size = 0;
	assert_ulong_equal(0, lzma_block_total_size(&block));

	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_ulong_equal(0, lzma_block_total_size(&block));
	block.compressed_size = 0x1000;

	// Invalid check value
	block.check = LZMA_CHECK_ID_MAX + 1;
	assert_ulong_equal(0, lzma_block_total_size(&block));

	// NULL block
	assert_ulong_equal(0, lzma_block_total_size(NULL));
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
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_encoder(NULL, &block));
	lzma_stream strm = LZMA_STREAM_INIT;

	// Test with NULL block
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_encoder(&strm, NULL));

	// Test with invalid version
	block.version = 2;
	assert_int_equal(LZMA_OPTIONS_ERROR,
			lzma_block_encoder(&strm, &block));
	block.version = 1;

	// Test with invalid check
	block.check = 0x1000;
	assert_int_equal(LZMA_PROG_ERROR,
			lzma_block_encoder(&strm, &block));
	block.check = 9;
	assert_int_equal(LZMA_UNSUPPORTED_CHECK,
			lzma_block_encoder(&strm, &block));
	block.check = LZMA_CHECK_CRC64;

	// Test with no filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR,
			lzma_block_encoder(&strm, &block));
	block.filters = filters;

	// Test encoding a block with LZMA_RUN
	assert_int_equal(LZMA_OK, lzma_block_encoder(&strm, &block));
	uint8_t out[BLOCK_SIZE * 2];

	strm.avail_in = BLOCK_SIZE;
	strm.avail_out = BLOCK_SIZE;
	strm.next_in = text_data.plain_data;
	strm.next_out = out;

	assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));
	// Test encoding with LZMA_SYNC_FLUSH
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm,
			LZMA_SYNC_FLUSH));

	// Test encoding with LZMA_FINISH
	strm.avail_in = BLOCK_SIZE;
	strm.avail_out = BLOCK_SIZE;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	// Compare encoded result with expected output
	uint8_t *expected_data = text_data.compressed_data;
	// Skip past stream header
	expected_data += LZMA_STREAM_HEADER_SIZE;
	// Skip past block header
	expected_data += lzma_block_header_size_decode(expected_data[0]);
	// Skip past lzma2 header
	expected_data += LZMA2_HEADER_MAX;
	uint8_t *out_data = out + LZMA2_HEADER_MAX;
	assert_n_array_equal(expected_data, out_data, 1000);
	lzma_end(&strm);

	// Test LZMA_SYNC_FLUSH with filters that do not support it
	// lzma1 does not support LZMA_SYNC_FLUSH
	lzma_options_lzma lzma1_block_ops;
	lzma_lzma_preset(&lzma1_block_ops, 6);
	lzma_options_delta delta_block_ops = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = 100
	};

	lzma_filter lzma1_block_filters[] = {
		{
			.id = LZMA_FILTER_DELTA,
			.options = &delta_block_ops
		},
		{
			.id = LZMA_FILTER_LZMA1,
			.options = &lzma1_block_ops
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};

	block.filters = lzma1_block_filters;

	assert_int_equal(LZMA_OK, lzma_block_encoder(&strm, &block));

	memzero(out, BLOCK_SIZE);

	strm.avail_in = BLOCK_SIZE;
	strm.avail_out = BLOCK_SIZE;
	strm.next_in = text_data.plain_data;
	strm.next_out = out;

	// Encode some data
	assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));
	// Try to sync flush (should fail)
	assert_int_equal(LZMA_OPTIONS_ERROR, lzma_code(&strm,
			LZMA_SYNC_FLUSH));
	lzma_end(&strm);
}


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
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_decoder(NULL, &block));

	// Test with NULL block
	lzma_stream strm = LZMA_STREAM_INIT;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_decoder(&strm, NULL));

	// Test with no filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_decoder(&strm, NULL));
	block.filters = decode_filters;

	// Test with invalid version
	block.version = 2;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_decoder(&strm, NULL));
	block.version = 1;

	// Test with invalid compressed_size
	block.compressed_size = 0;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_decoder(&strm, NULL));
	block.compressed_size = LZMA_VLI_UNKNOWN;

	uint8_t *in_buf = text_data.compressed_data;
	// Skip past stream header
	in_buf += LZMA_STREAM_HEADER_SIZE;
	// Decode block header
	block.header_size = lzma_block_header_size_decode(in_buf[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&block, NULL,
			in_buf));
	in_buf += block.header_size;

	// Decode text stream
	assert_int_equal(LZMA_OK, lzma_block_decoder(&strm, &block));
	uint8_t *out_buf = malloc(text_data.plain_size);

	strm.avail_in = text_data.compressed_size - (in_buf -
			text_data.compressed_data);
	strm.avail_out = text_data.plain_size;
	strm.next_in = in_buf;
	strm.next_out = out_buf;
	lzma_action action = LZMA_RUN;
	while (1) {
		if (strm.avail_in == 0)
			action = LZMA_FINISH;
		lzma_ret ret = lzma_code(&strm, action);
		if (ret == LZMA_STREAM_END) {
			break;
		}
		assert_int_equal(LZMA_OK, ret);
	}

	assert_int_equal(text_data.plain_size, strm.total_out);
	assert_n_array_equal(text_data.plain_data, out_buf,
			text_data.plain_size);

	free(out_buf);
	lzma_end(&strm);
	// TODO - attempt to decode bad blocks and expect errors
	// - Invalid checksum
	// - Corrupt bytes
	// - Invalid padding
}


static void
test_lzma_block_buffer_bound(void)
{
	// Test a few values and check if the result is > input
	for (size_t i = 0; i < 0x10000; i += 0x1500) {
		assert_true(lzma_block_buffer_bound(i) > i);
	}

	// Test 0 result with >= COMPRESSED_SIZE_MAX
	assert_ulong_equal(0, lzma_block_buffer_bound(COMPRESSED_SIZE_MAX));
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

	uint8_t *out_buf = malloc(text_data.compressed_size);
	size_t out_pos = 0;

	// Test with NULL block
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(NULL,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));

	// Test with NULL input
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, NULL, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));

	// Test with NULL output
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			NULL, &out_pos, text_data.compressed_size));

	// Test with NULL out_pos
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, NULL, text_data.compressed_size));

	// Test with outpos > outsize
	out_pos = 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, 0));
	out_pos = 0;

	// Test with invalid version
	block.version = 2;
	assert_int_equal(LZMA_OPTIONS_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));
	block.version = 1;

	// Test with invalid check
	block.check = 0x1000;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));
	block.check = 9;
	assert_int_equal(LZMA_UNSUPPORTED_CHECK, lzma_block_buffer_encode(
			&block, NULL, text_data.plain_data,
			text_data.plain_size, out_buf, &out_pos,
			text_data.compressed_size));
	block.check = LZMA_CHECK_CRC64;

	// Test with no filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));
	block.filters = filters;

	// Encode block
	assert_int_equal(LZMA_OK, lzma_block_buffer_encode(&block,
			NULL, text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, text_data.compressed_size));
	// Compare to expected
	// Compare encoded result with expected output
	uint8_t *expected_data = text_data.compressed_data;
	// Skip past stream header
	expected_data += LZMA_STREAM_HEADER_SIZE;
	// Skip past block header
	expected_data += lzma_block_header_size_decode(expected_data[0]);

	// Skip past block header in out_buf
	uint8_t *out_block = out_buf + lzma_block_header_size_decode(
			out_buf[0]);

	// Compare block data
	assert_n_array_equal(expected_data, out_block, out_pos -
			lzma_block_header_size_decode(out_buf[0]));

	free(out_buf);
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

	size_t out_size = lzma_block_buffer_bound(text_data.plain_size);
	uint8_t *out_buf = malloc(out_size);
	size_t out_pos = 0;

	// Test with NULL block
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(NULL,
			text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, out_size));

	// Test with NULL input
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(&block,
			NULL, text_data.plain_size,
			out_buf, &out_pos, out_size));

	// Test with NULL output
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			NULL, &out_pos, out_size));

	// Test with NULL out_pos
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			out_buf, NULL, out_size));

	// Test with outpos > outsize
	out_pos = 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, 0));
	out_pos = 0;

	// Test with invalid version
	block.version = 2;
	assert_int_equal(LZMA_OPTIONS_ERROR, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, out_size));
	block.version = 1;

	// Test with invalid check
	block.check = 0x1000;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, out_size));
	block.check = 9;
	assert_int_equal(LZMA_UNSUPPORTED_CHECK, lzma_block_uncomp_encode(
			&block, text_data.plain_data,
			text_data.plain_size, out_buf, &out_pos, out_size));
	block.check = LZMA_CHECK_CRC64;

	// Encode block
	assert_int_equal(LZMA_OK, lzma_block_uncomp_encode(&block,
			text_data.plain_data, text_data.plain_size,
			out_buf, &out_pos, out_size));
	// Compare to expected
	// Skip past block header and lzma2 header in out_buf
	// lzma2 header length discovered to be 3 after trial and error
	uint8_t *out_block = out_buf + lzma_block_header_size_decode(
			out_buf[0]) + LZMA2_HEADER_UNCOMPRESSED;

	// Compare block data through first lzma2 chunk
	assert_n_array_equal(text_data.plain_data, out_block, LZMA2_CHUNK_MAX);

	free(out_buf);
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
	uint8_t *out_buf = malloc(text_data.plain_size);

	size_t in_pos = 0;
	uint8_t *in_buf = text_data.compressed_data;
	// Skip past stream header
	in_buf += LZMA_STREAM_HEADER_SIZE;
	// Decode block header
	block.header_size = lzma_block_header_size_decode(in_buf[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&block, NULL,
			in_buf));
	in_buf += block.header_size;
	size_t in_buf_size = text_data.compressed_size - (size_t)
			(in_buf - text_data.compressed_data);

	// Test with NULL block
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(NULL,
			NULL, in_buf, &in_pos, in_buf_size, out_buf,
			&out_pos, text_data.plain_size));

	// Test with NULL input
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, NULL, &in_pos, in_buf_size, out_buf, &out_pos,
			text_data.plain_size));

	// Test with NULL output
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, in_buf, &in_pos, in_buf_size, NULL, &out_pos,
			text_data.plain_size));

	// Test with out_pos > out_size
	out_pos = 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, in_buf, &in_pos, in_buf_size, out_buf,
			&out_pos, 0));
	out_pos = 0;

	// Test with no filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, in_buf, &in_pos, in_buf_size, out_buf,
			&out_pos, text_data.plain_size));
	block.filters = decode_filters;

	// Test with invalid version
	block.version = 2;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, in_buf, &in_pos, in_buf_size, out_buf,
			&out_pos, text_data.plain_size));
	block.version = 1;

	// Test with invalid compressed_size
	block.compressed_size = 0;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_buffer_decode(&block,
			NULL, in_buf, &in_pos, in_buf_size, out_buf,
			&out_pos, text_data.plain_size));
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Test successful block decode
	assert_int_equal(LZMA_OK, lzma_block_buffer_decode(&block,
		NULL, in_buf, &in_pos, in_buf_size, out_buf, &out_pos,
		text_data.plain_size));

	assert_n_array_equal(text_data.plain_data, out_buf,
			text_data.plain_size);

	free(out_buf);
	// TODO - attempt to decode bad blocks and expect errors
	// - Invalid checksum
	// - Corrupt bytes
	// - Invalid padding
}


void test_block(void)
{
	assert_true(prepare_test_file_data(&text_data));
	lzma_options_lzma ops;
	lzma_lzma_preset(&ops, 6);
	filters[0].options = &ops;

	test_fixture_start();
	run_test(test_lzma_block_compressed_size);
	run_test(test_lzma_block_unpadded_size);
	run_test(test_lzma_block_total_size);
	run_test(test_lzma_block_encoder);
	run_test(test_lzma_block_decoder);
	run_test(test_lzma_block_buffer_bound);
	run_test(test_lzma_block_buffer_encode);
	run_test(test_lzma_block_uncomp_encode);
	run_test(test_lzma_block_buffer_decode);
	test_fixture_end();

	free_test_file_data(&text_data);
}
