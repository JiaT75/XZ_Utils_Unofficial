///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_check.c
/// \brief      Tests integrity checks
///
/// \todo       Add SHA256
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"


// These must be specified as numbers so that the test works on EBCDIC
// systems too.
// static const uint8_t test_string[9] = "123456789";
// static const uint8_t test_unaligned[12] = "xxx123456789";
static const uint8_t test_string[9] = { 49, 50, 51, 52, 53, 54, 55, 56, 57 };
static const uint8_t test_unaligned[12]
		= { 120, 120, 120, 49, 50, 51, 52, 53, 54, 55, 56, 57 };


static void
test_crc32(void)
{
	static const uint32_t test_vector = 0xCBF43926;

	// Test 1
	assert_int_equal(test_vector, lzma_crc32(test_string, sizeof(test_string), 0));
	
	// Test 2
	assert_int_equal(test_vector, lzma_crc32(test_unaligned + 3, sizeof(test_string), 0));

	// Test 3
	uint32_t crc = 0;
	for (size_t i = 0; i < sizeof(test_string); ++i)
		crc = lzma_crc32(test_string + i, 1, crc);
	
	assert_int_equal(test_vector, crc);
}


static void
test_crc64(void)
{
	static const uint64_t test_vector = 0x995DC9BBDF1939FA;

	// Test 1
	assert_ulong_equal(test_vector, lzma_crc64(test_string, sizeof(test_string), 0));

	// Test 2
	assert_ulong_equal(test_vector, lzma_crc64(test_unaligned + 3, sizeof(test_string), 0));

	// Test 3
	uint64_t crc = 0;
	for (size_t i = 0; i < sizeof(test_string); ++i)
		crc = lzma_crc64(test_string + i, 1, crc);

	assert_ulong_equal(test_vector, crc);
}


void
test_integrity_checks(void)
{
	test_fixture_start();
	run_test(test_crc32);
	run_test(test_crc64);
	test_fixture_end();
}
