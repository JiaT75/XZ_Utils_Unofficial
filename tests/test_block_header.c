///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_block_header.c
/// \brief      Tests Block Header coders
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "tuklib_integer.h"
#include "test_utils.h"


static uint8_t buf[LZMA_BLOCK_HEADER_SIZE_MAX];
static lzma_block known_options;
static lzma_block decoded_options;

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
code(void)
{
	assert_int_equal(lzma_block_header_encode(&known_options, buf), LZMA_OK);

	lzma_filter filters[LZMA_FILTERS_MAX + 1];
	memcrap(filters, sizeof(filters));
	memcrap(&decoded_options, sizeof(decoded_options));

	decoded_options.header_size = known_options.header_size;
	decoded_options.check = known_options.check;
	decoded_options.filters = filters;
	assert_int_equal(lzma_block_header_decode(&decoded_options, NULL, buf), LZMA_OK);

	assert_int_equal(known_options.compressed_size, decoded_options.compressed_size);
	assert_int_equal(known_options.uncompressed_size, decoded_options.uncompressed_size);

	for (size_t i = 0; known_options.filters[i].id
			!= LZMA_VLI_UNKNOWN; ++i)
		assert_int_equal(known_options.filters[i].id, filters[i].id);

	for (size_t i = 0; i < LZMA_FILTERS_MAX; ++i)
		free(decoded_options.filters[i].options);
}


static void
test1(void)
{
	known_options = (lzma_block){
		.check = LZMA_CHECK_NONE,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.filters = NULL,
	};

	assert_int_equal(lzma_block_header_size(&known_options), LZMA_PROG_ERROR);

	known_options.filters = filters_none;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_PROG_ERROR);

	known_options.filters = filters_five;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_PROG_ERROR);

	known_options.filters = filters_one;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);

	// Some invalid value, which gets ignored.
	known_options.check = (lzma_check)(99);
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);

	known_options.compressed_size = 5;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);

	known_options.compressed_size = 0; // Cannot be zero.
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_PROG_ERROR);

	// LZMA_VLI_MAX is too big to keep the total size of the Block
	// a valid VLI, but lzma_block_header_size() is not meant
	// to validate it. (lzma_block_header_encode() must validate it.)
	known_options.compressed_size = LZMA_VLI_MAX;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);

	known_options.compressed_size = LZMA_VLI_UNKNOWN;
	known_options.uncompressed_size = 0;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);

	known_options.uncompressed_size = LZMA_VLI_MAX + 1;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_PROG_ERROR);
}


static void
test2(void)
{
	known_options = (lzma_block){
		.check = LZMA_CHECK_CRC32,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.filters = filters_four,
	};

	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);
	code();

	known_options.compressed_size = 123456;
	known_options.uncompressed_size = 234567;
	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);
	code();

	// We can make the sizes smaller while keeping the header size
	// the same.
	known_options.compressed_size = 12;
	known_options.uncompressed_size = 23;
	code();
}


static void
test3(void)
{
	known_options = (lzma_block){
		.check = LZMA_CHECK_CRC32,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.filters = filters_one,
	};

	assert_int_equal(lzma_block_header_size(&known_options), LZMA_OK);
	known_options.header_size += 4;
	assert_int_equal(lzma_block_header_encode(&known_options, buf), LZMA_OK);

	lzma_filter filters[LZMA_FILTERS_MAX + 1];
	decoded_options.header_size = known_options.header_size;
	decoded_options.check = known_options.check;
	decoded_options.filters = filters;

	// Wrong size
	++buf[0];
	assert_int_equal(lzma_block_header_decode(&decoded_options, NULL, buf), LZMA_PROG_ERROR);
	--buf[0];

	// Wrong CRC32
	buf[known_options.header_size - 1] ^= 1;
	assert_int_equal(lzma_block_header_decode(&decoded_options, NULL, buf), LZMA_DATA_ERROR);
	buf[known_options.header_size - 1] ^= 1;

	// Unsupported filter
	// NOTE: This may need updating when new IDs become supported.
	buf[2] ^= 0x1F;
	write32le(buf + known_options.header_size - 4,
			lzma_crc32(buf, known_options.header_size - 4, 0));
	assert_int_equal(lzma_block_header_decode(&decoded_options, NULL, buf), LZMA_OPTIONS_ERROR);
	buf[2] ^= 0x1F;

	// Non-nul Padding
	buf[known_options.header_size - 4 - 1] ^= 1;
	write32le(buf + known_options.header_size - 4,
			lzma_crc32(buf, known_options.header_size - 4, 0));
	assert_int_equal(lzma_block_header_decode(&decoded_options, NULL, buf), LZMA_OPTIONS_ERROR);
	buf[known_options.header_size - 4 - 1] ^= 1;
}


void
test_block_header_coders(void)
{
	test_fixture_start();
	assert_false(lzma_lzma_preset(&opt_lzma, 1));
	run_test(test1);
	run_test(test2);
	run_test(test3);
	test_fixture_end();
}
