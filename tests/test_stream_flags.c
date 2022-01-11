///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_stream_flags.c
/// \brief      Tests Stream Header and Stream Footer coders
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "test_utils.h"
#include "tuklib_integer.h"
#include <stdbool.h>


static lzma_stream_flags known_flags;
static lzma_stream_flags decoded_flags;
static uint8_t buffer[LZMA_STREAM_HEADER_SIZE];


static bool
validate(void)
{
	// TODO: This could require the specific error type as an argument.
	// We could also test that lzma_stream_flags_compare() gives
	// the correct return values in different situations.
	return lzma_stream_flags_compare(&known_flags, &decoded_flags)
			!= LZMA_OK;
}


static bool
test_header_decoder(lzma_ret expected_ret)
{
	memcrap(&decoded_flags, sizeof(decoded_flags));

	if (lzma_stream_header_decode(&decoded_flags, buffer) != expected_ret)
		return true;

	if (expected_ret != LZMA_OK)
		return false;

	// Header doesn't have Backward Size, so make
	// lzma_stream_flags_compare() ignore it.
	decoded_flags.backward_size = LZMA_VLI_UNKNOWN;
	return validate();
}


static void
test_header(void)
{
	memcrap(buffer, sizeof(buffer));
	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer), LZMA_OK);
	assert_false(test_header_decoder(LZMA_OK));
}


static bool
test_footer_decoder(lzma_ret expected_ret)
{
	memcrap(&decoded_flags, sizeof(decoded_flags));

	if (lzma_stream_footer_decode(&decoded_flags, buffer) != expected_ret)
		return true;

	if (expected_ret != LZMA_OK)
		return false;

	return validate();
}


static void
test_footer(void)
{
	memcrap(buffer, sizeof(buffer));
	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer), LZMA_OK);
	assert_false(test_footer_decoder(LZMA_OK));
}


static void
test_encode_invalid(void)
{
	known_flags.check = (lzma_check)(LZMA_CHECK_ID_MAX + 1);
	known_flags.backward_size = 1024;

	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);

	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);

	known_flags.check = (lzma_check)(-1);

	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);

	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);

	known_flags.check = LZMA_CHECK_NONE;
	known_flags.backward_size = 0;

	// Header encoder ignores backward_size.
	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer), LZMA_OK);

	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);

	known_flags.backward_size = LZMA_VLI_MAX;

	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer), LZMA_OK);

	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer),
			LZMA_PROG_ERROR);
}


static void
test_decode_invalid(void)
{
	known_flags.check = LZMA_CHECK_NONE;
	known_flags.backward_size = 1024;

	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer), LZMA_OK);

	// Test 1 (invalid Magic Bytes)
	buffer[5] ^= 1;
	assert_false(test_header_decoder(LZMA_FORMAT_ERROR));
	buffer[5] ^= 1;

	// Test 2a (valid CRC32)
	uint32_t crc = lzma_crc32(buffer + 6, 2, 0);
	write32le(buffer + 8, crc);
	assert_false(test_header_decoder(LZMA_OK));

	// Test 2b (invalid Stream Flags with valid CRC32)
	buffer[6] ^= 0x20;
	crc = lzma_crc32(buffer + 6, 2, 0);
	write32le(buffer + 8, crc);
	assert_false(test_header_decoder(LZMA_OPTIONS_ERROR));

	// Test 3 (invalid CRC32)
	assert_int_equal(lzma_stream_header_encode(&known_flags, buffer), LZMA_OK);
	buffer[9] ^= 1;
	assert_false(test_header_decoder(LZMA_DATA_ERROR));

	// Test 4 (invalid Stream Flags with valid CRC32)
	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer), LZMA_OK);
	buffer[9] ^= 0x40;
	crc = lzma_crc32(buffer + 4, 6, 0);
	write32le(buffer, crc);
	assert_false(test_footer_decoder(LZMA_OPTIONS_ERROR));

	// Test 5 (invalid Magic Bytes)
	assert_int_equal(lzma_stream_footer_encode(&known_flags, buffer), LZMA_OK);
	buffer[11] ^= 1;
	assert_false(test_footer_decoder(LZMA_FORMAT_ERROR));
}


void test_stream_header_and_footer_coders(void)
{
	test_fixture_start();
	// Valid headers
	known_flags.backward_size = 1024;
	for (lzma_check check = LZMA_CHECK_NONE;
			check <= LZMA_CHECK_ID_MAX; ++check) {
		run_test(test_header);
		run_test(test_footer);
	}

	// Invalid headers
	run_test(test_encode_invalid);
	run_test(test_decode_invalid);
	test_fixture_end();
}
