///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_stream_flags.c
/// \brief      Tests Stream Header and Stream Footer coders
//
//  Author:     Lasse Collin
//              Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "tests.h"

// Size of the Stream Flags field 
// (taken from src/liblzma/common/stream_flags_common.h)
#define LZMA_STREAM_FLAGS_SIZE 2
// Header and footer magic bytes for .xz file format
// (taken from src/liblzma/common/stream_flags_common.c)
const uint8_t lzma_header_magic[6] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
const uint8_t lzma_footer_magic[2] = { 0x59, 0x5A };

static void
stream_header_encode_helper(lzma_check check)
{
	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = check
	};
	uint8_t header[LZMA_STREAM_HEADER_SIZE];

	// Encode stream header
	assert_lzma_ret(lzma_stream_header_encode(&flags, header), LZMA_OK);

	// Stream header must start with magic bytes
	const uint32_t magic_size = ARRAY_SIZE(lzma_header_magic);
	assert_array_eq(header, lzma_header_magic, magic_size);

	// Next must come stream flags
	const uint8_t *encoded_stream_flags = header + magic_size;
	// First byte is always NULL byte
	// Second byte must be check ID
	const uint8_t expected_stream_flags[] = { 0, check };
	assert_array_eq(encoded_stream_flags, expected_stream_flags,
			LZMA_STREAM_FLAGS_SIZE);

	// Last part is the CRC32 of the stream flags
	const uint8_t* crc_ptr = encoded_stream_flags +
			LZMA_STREAM_FLAGS_SIZE;
	const uint32_t expected_crc = lzma_crc32(expected_stream_flags,
			LZMA_STREAM_FLAGS_SIZE, 0);
	assert_uint_eq(read32le(crc_ptr), expected_crc);
}


static void
test_lzma_stream_header_encode(void)
{
	for (int i = 0; i < LZMA_CHECK_ID_MAX; i++)
		stream_header_encode_helper(i);

	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = LZMA_CHECK_CRC32
	};
	uint8_t header[LZMA_STREAM_HEADER_SIZE];

	// Should fail is version > LZMA_STREAM_FLAGS_VERSION
	flags.version = LZMA_STREAM_FLAGS_VERSION + 1;
	assert_lzma_ret(lzma_stream_header_encode(&flags, header),
			LZMA_OPTIONS_ERROR);
	flags.version = LZMA_STREAM_FLAGS_VERSION;

	// Should fail if check is invalid
	flags.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_stream_header_encode(&flags, header),
			LZMA_PROG_ERROR);
	flags.check = LZMA_CHECK_CRC32;

	// Should pass even if backwards size is invalid
	flags.backward_size = LZMA_VLI_MAX + 1;
	assert_lzma_ret(lzma_stream_header_encode(&flags, header), LZMA_OK);
}


static void
stream_footer_encode_helper(lzma_check check)
{
	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = check,
		.backward_size = LZMA_BACKWARD_SIZE_MIN
	};
	uint8_t footer[LZMA_STREAM_HEADER_SIZE];

	// Encode stream footer
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer), LZMA_OK);

	// Stream footer must start with CRC32
	const uint32_t crc = read32le(footer);
	const uint32_t expected_crc = lzma_crc32(footer + sizeof(uint32_t),
			LZMA_STREAM_HEADER_SIZE - (sizeof(uint32_t) +
			ARRAY_SIZE(lzma_footer_magic)), 0);
	assert_uint_eq(crc, expected_crc);

	// Next the backwards size
	const uint32_t backwards_size = read32le(footer + sizeof(uint32_t));
	const uint32_t expected_backwards_size = flags.backward_size / 4 - 1;
	assert_uint_eq(backwards_size, expected_backwards_size);
	// Next the stream flags
	const uint8_t *stream_flags = footer + sizeof(uint32_t) * 2;
	// First byte must be NULL
	assert_uint_eq(stream_flags[0], 0);
	// Second byte must be the check value
	assert_uint_eq(stream_flags[1], check);

	// And ends with footer magic bytes
	const uint8_t *expected_footer_magic = stream_flags +
			LZMA_STREAM_FLAGS_SIZE;
	assert_array_eq(expected_footer_magic, lzma_footer_magic,
			ARRAY_SIZE(lzma_footer_magic));
}


static void
test_lzma_stream_footer_encode(void)
{
	for (int i = 0; i < LZMA_CHECK_ID_MAX; i++)
		stream_footer_encode_helper(i);

	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.backward_size = LZMA_BACKWARD_SIZE_MIN,
		.check = LZMA_CHECK_CRC32
	};
	uint8_t footer[LZMA_STREAM_HEADER_SIZE];

	// Should fail is version > LZMA_STREAM_FLAGS_VERSION
	flags.version = LZMA_STREAM_FLAGS_VERSION + 1;
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer),
			LZMA_OPTIONS_ERROR);
	flags.version = LZMA_STREAM_FLAGS_VERSION;

	// Should fail if check is invalid
	flags.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer),
			LZMA_PROG_ERROR);

	// Should fail if backward size is invalid
	flags.backward_size -= 1;
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer),
			LZMA_PROG_ERROR);
	flags.backward_size += 2;
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer),
			LZMA_PROG_ERROR);
	flags.backward_size = LZMA_BACKWARD_SIZE_MAX + 4;
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer),
			LZMA_PROG_ERROR);
}


static void
stream_header_decode_helper(lzma_check check)
{
	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = check
	};
	uint8_t header[LZMA_STREAM_HEADER_SIZE];
	assert_lzma_ret(lzma_stream_header_encode(&flags, header), LZMA_OK);

	lzma_stream_flags dest_flags;
	assert_lzma_ret(lzma_stream_header_decode(&dest_flags, header),
			LZMA_OK);

	// Version should be 0
	assert_uint_eq(dest_flags.version, 0);
	// backward size should be LZMA_VLI_UNKNOWN
	assert_uint_eq(dest_flags.backward_size, LZMA_VLI_UNKNOWN);
	// Check must equal parameter
	assert_uint_eq(dest_flags.check, check);
}


static void
test_lzma_stream_header_decode(void)
{
	for (int i = 0; i < LZMA_CHECK_ID_MAX; i++)
		stream_header_decode_helper(i);

	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = LZMA_CHECK_CRC32
	};
	uint8_t header[LZMA_STREAM_HEADER_SIZE];
	lzma_stream_flags dest;

	// First encode known flags to header buffer
	assert_lzma_ret(lzma_stream_header_encode(&flags, header), LZMA_OK);

	// Should fail if magic bytes do not match
	header[0] ^= 1;
	assert_lzma_ret(lzma_stream_header_decode(&dest, header),
			LZMA_FORMAT_ERROR);
	header[0] ^= 1;

	// Should fail if reserved bits are set
	uint8_t *stream_flags = header + ARRAY_SIZE(lzma_header_magic);
	stream_flags[0] = 1;
	// Need to adjust CRC32 after making a change since the CRC32
	// is checked before the stream flags
	uint32_t *crc32_ptr = (uint32_t*) (stream_flags +
			LZMA_STREAM_FLAGS_SIZE);
	const uint32_t crc_orig = *crc32_ptr;
	*crc32_ptr = lzma_crc32(stream_flags, LZMA_STREAM_FLAGS_SIZE, 0);
	assert_lzma_ret(lzma_stream_header_decode(&dest, header),
			LZMA_OPTIONS_ERROR);
	stream_flags[0] = 0;
	*crc32_ptr = crc_orig;

	// Should fail if upper bits of check ID are set
	stream_flags[1] |= 0xF0;
	*crc32_ptr = lzma_crc32(stream_flags, LZMA_STREAM_FLAGS_SIZE, 0);
	assert_lzma_ret(lzma_stream_header_decode(&dest, header),
			LZMA_OPTIONS_ERROR);
	stream_flags[1] = flags.check;
	*crc32_ptr = crc_orig;

	// Should fail if CRC32 does not match
	// First, alter a byte in the stream header
	stream_flags[0] = 1;
	assert_lzma_ret(lzma_stream_header_decode(&dest, header),
			LZMA_DATA_ERROR);
	stream_flags[0] = 0;
	// Next, change the CRC32
	*crc32_ptr ^= 1;
	assert_lzma_ret(lzma_stream_header_decode(&dest, header),
			LZMA_DATA_ERROR);
}


static void
stream_footer_decode_helper(lzma_check check)
{
	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.backward_size = LZMA_BACKWARD_SIZE_MIN,
		.check = check
	};
	uint8_t footer[LZMA_STREAM_HEADER_SIZE];
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer), LZMA_OK);

	lzma_stream_flags dest_flags;
	assert_lzma_ret(lzma_stream_footer_decode(&dest_flags, footer),
			LZMA_OK);

	// Version should be 0
	assert_uint_eq(dest_flags.version, 0);
	// Backward size should equal the value from the flags
	assert_uint_eq(dest_flags.backward_size, flags.backward_size);
	// Check must equal parameter
	assert_uint_eq(dest_flags.check, check);
}


static void
test_lzma_stream_footer_decode(void)
{
	for (int i = 0; i < LZMA_CHECK_ID_MAX; i++)
		stream_footer_decode_helper(i);

	lzma_stream_flags flags = {
		.version = LZMA_STREAM_FLAGS_VERSION,
		.check = LZMA_CHECK_CRC32,
		.backward_size = LZMA_BACKWARD_SIZE_MIN
	};
	uint8_t footer[LZMA_STREAM_HEADER_SIZE];
	lzma_stream_flags dest;

	// First encode known flags to footer buffer
	assert_lzma_ret(lzma_stream_footer_encode(&flags, footer), LZMA_OK);

	// Should fail if magic bytes do not match
	footer[LZMA_STREAM_HEADER_SIZE - 1] ^= 1;
	assert_lzma_ret(lzma_stream_footer_decode(&dest, footer),
			LZMA_FORMAT_ERROR);
	footer[LZMA_STREAM_HEADER_SIZE - 1] ^= 1;

	// Should fail if reserved bits are set
	// In the footer, the stream flags follow the CRC32 (4 bytes)
	// and the backward size (4 bytes)
	uint8_t *stream_flags = footer + sizeof(uint32_t) * 2;
	stream_flags[0] = 1;
	// Need to adjust the CRC32 so it will not fail that check instead
	uint32_t *crc32_ptr = (uint32_t*) footer;
	const uint32_t crc_orig = *crc32_ptr;
	uint8_t *backward_size = footer + sizeof(uint32_t);
	*crc32_ptr = lzma_crc32(backward_size, sizeof(uint32_t) +
			LZMA_STREAM_FLAGS_SIZE, 0);
	assert_lzma_ret(lzma_stream_footer_decode(&dest, footer),
			LZMA_OPTIONS_ERROR);
	stream_flags[0] = 0;
	*crc32_ptr = crc_orig;

	// Should fail if upper bits of check ID are set
	stream_flags[1] |= 0xF0;
	*crc32_ptr = lzma_crc32(backward_size, sizeof(uint32_t) +
			LZMA_STREAM_FLAGS_SIZE, 0);
	assert_lzma_ret(lzma_stream_footer_decode(&dest, footer),
			LZMA_OPTIONS_ERROR);
	stream_flags[1] = flags.check;
	*crc32_ptr = crc_orig;

	// Should fail if CRC32 does not match
	// First, alter a byte in the stream footer
	stream_flags[0] = 1;
	assert_lzma_ret(lzma_stream_footer_decode(&dest, footer),
			LZMA_DATA_ERROR);
	stream_flags[0] = 0;
	// Next, change the CRC32
	*crc32_ptr ^= 1;
	assert_lzma_ret(lzma_stream_footer_decode(&dest, footer),
			LZMA_DATA_ERROR);
}


static void
test_lzma_stream_flags_compare(void)
{
	lzma_stream_flags first = {
		.version = 0,
		.backward_size = LZMA_BACKWARD_SIZE_MIN,
		.check = LZMA_CHECK_CRC32
	};

	lzma_stream_flags second = {
		.version = 0,
		.backward_size = LZMA_BACKWARD_SIZE_MIN,
		.check = LZMA_CHECK_CRC32
	};

	// First test should pass
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second), LZMA_OK);

	// Altering either version should cause an error
	first.version = LZMA_STREAM_FLAGS_VERSION + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_DATA_ERROR);
	second.version = LZMA_STREAM_FLAGS_VERSION + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_OPTIONS_ERROR);
	first.version = 0;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_DATA_ERROR);
	second.version = 0;

	// Check types must be under the maximum
	first.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	second.check = LZMA_CHECK_ID_MAX + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	first.check = LZMA_CHECK_CRC32;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	second.check = LZMA_CHECK_CRC32;

	// Check types must be equal
	for (uint32_t i = 0; i < LZMA_CHECK_ID_MAX; i++) {
		first.check = i;
		if (i == second.check)
			assert_lzma_ret(lzma_stream_flags_compare(&first,
					&second), LZMA_OK);
		else
			assert_lzma_ret(lzma_stream_flags_compare(&first,
					&second), LZMA_DATA_ERROR);
	}
	first.check = LZMA_CHECK_CRC32;

	// Backward size comparison is skipped if either are LZMA_VLI_UNKNOWN
	first.backward_size = LZMA_VLI_UNKNOWN;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second), LZMA_OK);
	second.backward_size = LZMA_VLI_MAX + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second), LZMA_OK);
	second.backward_size = LZMA_BACKWARD_SIZE_MIN;

	// Backward sizes need to be valid
	first.backward_size = LZMA_VLI_MAX + 4;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	second.backward_size = LZMA_VLI_MAX + 4;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	first.backward_size = LZMA_BACKWARD_SIZE_MIN;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
	second.backward_size = LZMA_BACKWARD_SIZE_MIN;

	// Backward sizes must be equal
	second.backward_size = first.backward_size + 4;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_DATA_ERROR);

	// Should fail if backward are > LZMA_BACKWARD_SIZE_MAX
	// even though they are equal
	first.backward_size = LZMA_BACKWARD_SIZE_MAX + 1;
	second.backward_size = LZMA_BACKWARD_SIZE_MAX + 1;
	assert_lzma_ret(lzma_stream_flags_compare(&first, &second),
			LZMA_PROG_ERROR);
}


extern int
main(int argc, char **argv)
{
	tuktest_start(argc, argv);
	tuktest_run(test_lzma_stream_header_encode);
	tuktest_run(test_lzma_stream_footer_encode);
	tuktest_run(test_lzma_stream_header_decode);
	tuktest_run(test_lzma_stream_footer_decode);
	tuktest_run(test_lzma_stream_flags_compare);
	return tuktest_end();
}
