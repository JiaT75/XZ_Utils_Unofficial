///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_block_header.c
/// \brief      Tests Block Header coders
//
//  Author:     Lasse Collin
//              Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "tuklib_integer.h"
#include "test_utils.h"
#include "sysdefs.h"
#include "test_lzma_filter_utils.h"


static lzma_options_lzma opt_lzma;

static lzma_filter filters_none[1] = {
	{
		.id = LZMA_VLI_UNKNOWN,
	},
};


static lzma_filter filters_one[2] = {
	{
		.id = LZMA_FILTER_LZMA2,
		.options = &opt_lzma,
	}, {
		.id = LZMA_VLI_UNKNOWN,
	}
};


static lzma_filter filters_four[5] = {
	{
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_LZMA2,
		.options = &opt_lzma,
	}, {
		.id = LZMA_VLI_UNKNOWN,
	}
};


static lzma_filter filters_five[6] = {
	{
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_X86,
		.options = NULL,
	}, {
		.id = LZMA_FILTER_LZMA2,
		.options = &opt_lzma,
	}, {
		.id = LZMA_VLI_UNKNOWN,
	}
};


static void
test_lzma_block_header_size(void)
{
	lzma_block block;
	memzero(&block, sizeof(lzma_block));

	block.filters = filters_one;
	block.compressed_size = LZMA_VLI_UNKNOWN;
	block.uncompressed_size = LZMA_VLI_UNKNOWN;
	block.check = LZMA_CHECK_CRC32;

	// Test that all initial options are valid
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);


	// Test invalid version number
	for (int i = 2; i < 20; i++) {
		block.version = i;
		assert_int_equal(LZMA_OPTIONS_ERROR,
				lzma_block_header_size(&block));
	}

	block.version = 1;

	// Test invalid compressed size
	block.compressed_size = 0;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));

	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));
	block.compressed_size = LZMA_VLI_UNKNOWN;

	// Test invalid uncompressed size
	block.uncompressed_size = LZMA_VLI_MAX + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));
	block.uncompressed_size = LZMA_VLI_MAX;

	// Test invalid filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));

	block.filters = filters_none;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));

	block.filters = filters_five;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_size(&block));

	block.filters = filters_one;

	// Test setting lzma_compressed_size to something valid
	block.compressed_size = 0x4096;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);

	// Test setting lzma_uncompressed_size to something valid
	block.uncompressed_size = 0x4096;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);

	// This should pass, but header_size will be an invalid value
	// because the total block size will not be able to fit in a valid
	// lzma_vli
	block.compressed_size = LZMA_VLI_MAX;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);

	// Use an invalid value for a filter option (should still pass)
	lzma_options_lzma bad_ops;
	assert_false(lzma_lzma_preset(&bad_ops, 1));
	bad_ops.pb = 0x1000;

	lzma_filter bad_filters[2] = {
		{
			.id = LZMA_FILTER_LZMA2,
			.options = &bad_ops
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};

	block.filters = bad_filters;

	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);

	// Use an invalid block option
	block.check = 0x1000;
	block.ignore_check = false;

	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_true(block.header_size >= LZMA_BLOCK_HEADER_SIZE_MIN &&
			block.header_size <= LZMA_BLOCK_HEADER_SIZE_MAX &&
			block.header_size % 4 == 0);
}


static void
test_lzma_block_header_encode(void)
{
	lzma_block block;
	memzero(&block, sizeof(lzma_block));

	block.filters = filters_one;
	block.compressed_size = LZMA_VLI_UNKNOWN;
	block.uncompressed_size = LZMA_VLI_UNKNOWN;
	block.check = LZMA_CHECK_CRC32;
	block.version = 1;

	// Ensure all block options are valid before changes are tested
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));

	uint8_t out[LZMA_BLOCK_HEADER_SIZE_MAX];

	// Test invalid block version
	for (int i = 2; i < 20; i++) {
		block.version = i;
		assert_int_equal(LZMA_PROG_ERROR,
				lzma_block_header_encode(&block, out));
	}
	block.version = 1;
	// Test invalid header size (< min, > max, % 4 != 0)
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN - 4;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MIN + 2;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.header_size = LZMA_BLOCK_HEADER_SIZE_MAX + 4;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	// Test invalid compressed_size
	block.compressed_size = 0;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.compressed_size = LZMA_VLI_MAX + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	// This test passes test_lzma_block_header_size, but should
	// fail here because there is not enough space to encode the
	// proper block size because the total size is too big to fit
	// in an lzma_vli
	block.compressed_size = LZMA_VLI_MAX;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.compressed_size = LZMA_VLI_UNKNOWN;
	// Test invalid uncompressed size
	block.uncompressed_size = LZMA_VLI_MAX + 1;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.uncompressed_size = LZMA_VLI_UNKNOWN;
	// Test invalid block check
	block.check = 0x1000;
	block.ignore_check = false;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	block.check = LZMA_CHECK_CRC32;
	// Test invalid filters
	block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));

	block.filters = filters_none;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));

	block.filters = filters_five;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_encode(
			&block, out));
	// Test valid encoding and verify bytes of block header
	// More complicated tests for encoding headers are included
	// in test_lzma_block_header_decode
	block.filters = filters_one;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block, out));
	// First read block header size from out and verify
	// that it == (encoded size + 1) * 4
	lzma_vli header_size = (out[0] + 1) * 4;
	assert_int_equal(block.header_size, header_size);
	// Next read block flags
	uint8_t flags = out[1];
	// Should have number of filters = 1
	assert_int_equal(1, (flags & 0x3) + 1);
	// Bits 2-7 must be empty not set
	assert_int_equal(0, flags & (0xFF - 0x3));
	// Verify filter flags
	// Decode Filter ID
	lzma_vli filter_id = 0;
	size_t pos = 2;
	assert_int_equal(lzma_vli_decode(&filter_id, NULL, out,
			&pos, header_size), LZMA_OK);
	assert_ulong_equal(filters_one[0].id, filter_id);
	// Decode Size of Properties
	lzma_vli prop_size = 0;
	assert_int_equal(lzma_vli_decode(&prop_size, NULL, out,
			&pos, header_size), LZMA_OK);
	// LZMA2 has 1 byte prop size
	assert_ulong_equal(1, prop_size);
	uint8_t expected_filter_props = 0;
	assert_int_equal(LZMA_OK, lzma_properties_encode(filters_one,
			&expected_filter_props));
	assert_int_equal(expected_filter_props, out[pos]);
	pos++;
	// Check NULL padding
	for (uint32_t i = pos; i < header_size - 4; i++)
		assert_int_equal(0, out[i]);
	// Check CRC32
	assert_int_equal(lzma_crc32(out, header_size - 4, 0),
			read32le(&out[header_size - 4]));
}


static void
compare_blocks(lzma_block *block_expected, lzma_block *block_actual)
{
	assert_int_equal(block_expected->version, block_actual->version);
	assert_int_equal(block_expected->compressed_size,
			block_actual->compressed_size);
	assert_int_equal(block_expected->uncompressed_size,
			block_actual->uncompressed_size);
	assert_int_equal(block_expected->check, block_actual->check);
	assert_int_equal(block_expected->header_size,
			block_actual->header_size);
	// Compare filter IDs
	assert_true(block_expected->filters && block_actual->filters);
	lzma_filter expected_filter = block_expected->filters[0];
	int filter_count = 0;
	while (expected_filter.id != LZMA_VLI_UNKNOWN) {
		assert_ulong_equal(expected_filter.id,
				block_actual->filters[filter_count].id);
		expected_filter = block_expected->filters[++filter_count];
	}
	assert_ulong_equal(LZMA_VLI_UNKNOWN,
			block_actual->filters[filter_count].id);
}

static void
test_lzma_block_header_decode(void)
{
	lzma_block block;
	memzero(&block, sizeof(lzma_block));

	block.filters = filters_one;
	block.compressed_size = LZMA_VLI_UNKNOWN;
	block.uncompressed_size = LZMA_VLI_UNKNOWN;
	block.check = LZMA_CHECK_CRC32;
	block.version = 0;

	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	// Encode block header with simple options
	uint8_t out[LZMA_BLOCK_HEADER_SIZE_MAX];
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block, out));

	// Decode block header and check that the options match
	lzma_block decode_block;
	lzma_filter decode_filters[LZMA_FILTERS_MAX + 1];
	decode_block.version = 0;
	decode_block.filters = decode_filters;
	decode_block.check = LZMA_CHECK_CRC32;
	decode_block.header_size = lzma_block_header_size_decode(out[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&decode_block,
			NULL, out));
	compare_blocks(&block, &decode_block);

	//Reset output buffer and decode block
	memzero(out, LZMA_BLOCK_HEADER_SIZE_MAX);
	memzero(&decode_block, sizeof(lzma_block));
	decode_block.filters = decode_filters;
	decode_block.check = LZMA_CHECK_CRC32;

	// Test with compressed size set
	block.compressed_size = 4096;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block, out));
	decode_block.header_size = lzma_block_header_size_decode(out[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&decode_block,
			NULL, out));
	compare_blocks(&block, &decode_block);

	memzero(out, LZMA_BLOCK_HEADER_SIZE_MAX);
	memzero(&decode_block, sizeof(lzma_block));
	decode_block.filters = decode_filters;
	decode_block.check = LZMA_CHECK_CRC32;

	// Test with uncompressed size set
	block.uncompressed_size = 4096;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block, out));
	decode_block.header_size = lzma_block_header_size_decode(out[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&decode_block,
			NULL, out));
	compare_blocks(&block, &decode_block);

	memzero(out, LZMA_BLOCK_HEADER_SIZE_MAX);
	memzero(&decode_block, sizeof(lzma_block));
	decode_block.filters = decode_filters;
	decode_block.check = LZMA_CHECK_CRC32;

	// Test with multiple filters
	block.filters = filters_four;
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block, out));
	decode_block.header_size = lzma_block_header_size_decode(out[0]);
	assert_int_equal(LZMA_OK, lzma_block_header_decode(&decode_block,
			NULL, out));
	compare_blocks(&block, &decode_block);

	memzero(&decode_block, sizeof(lzma_block));
	decode_block.filters = decode_filters;
	decode_block.check = LZMA_CHECK_CRC32;
	decode_block.header_size = lzma_block_header_size_decode(out[0]);

	// Test with bad version (decoder sets to version it can support)
	decode_block.version = 2;
	assert_int_equal(LZMA_OK, lzma_block_header_decode(
			&decode_block, NULL, out));

	// Test with NULL filters
	decode_block.version = 0;
	decode_block.filters = NULL;
	assert_int_equal(LZMA_PROG_ERROR, lzma_block_header_decode(
			&decode_block, NULL, out));
	decode_block.filters = filters_four;

	// Test bad CRC value
	out[decode_block.header_size - 1] -= 10;
	assert_int_equal(LZMA_DATA_ERROR, lzma_block_header_decode(
			&decode_block, NULL, out));
	out[decode_block.header_size - 1] += 10;

	// Test non-NULL padding
	out[decode_block.header_size - 5] = 1;
	// Recompute crc
	write32le(&out[decode_block.header_size - 4], lzma_crc32(out,
			decode_block.header_size - 4, 0));
	assert_int_equal(LZMA_OPTIONS_ERROR, lzma_block_header_decode(
			&decode_block, NULL, out));
}

void
test_block_headers(void)
{
	test_fixture_start();
	run_test(test_lzma_block_header_size);
	run_test(test_lzma_block_header_encode);
	run_test(test_lzma_block_header_decode);
	test_fixture_end();
}
