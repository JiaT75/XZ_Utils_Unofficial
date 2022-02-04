///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_lzma_properties.c
/// \brief      Tests liblzma filter properties API functions from
///             src/liblzma/api/lzma/filter.h
///
/// \todo	Add more in-depth tests for LZMA1 and LZMA2 properties
///		by varying the dictionary size and pb, lp, and lc values
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "test_utils.h"
#include "config.h"
#include "test_lzma_filter_utils.h"
#include <stdlib.h>

// Loop through the basic_filters and ensure they all
// have valid property sizes
// Next loop until LZMA_FILTER_MAX_ID_CHECK and ensure
// filters not in basic_filters are invalid
static void
test_lzma_properties_size(void)
{
	for(int i = 0; i < sizeof(basic_filters) / sizeof(lzma_filter); i++){
		size_t size = 0;
		assert_int_equal(lzma_properties_size(&size, &basic_filters[i]),
					LZMA_OK);
		assert_true(size != UINT64_MAX);
	}

	for(int i = 0; i < LZMA_FILTER_MAX_ID_CHECK; i++){
		int valid_id = 0;
		for(int j = 0; j < sizeof(basic_filters) / sizeof(lzma_filter); j++){
			if(i == basic_filters[j].id){
				valid_id = 1;
				break;
			}
		}

		if(!valid_id){
			size_t size = 0;
			lzma_filter invalid_filter = {
				.id = i,
				.options = NULL
			};
			assert_int_equal(lzma_properties_size(&size, &invalid_filter),
					LZMA_OPTIONS_ERROR);
		}
	}
}


// Simple, but not the most efficient and does not do overflow checking
// Should only be used for small exponentiation,
// so efficiency should not be an issue
static uint32_t
simple_exponentiation(uint32_t base, uint32_t power)
{
	if(base == 0){
		return base;
	}
	else if(power == 0 || base == 1){
		return 1;
	}

	uint32_t result = base;
	for(int i = 0; i < power; i++){
		result *= base;
	}
	return result;
}


// Helper function for test_lzma_properties_encode to test
// all support bcj filters since they all have the same
// properties
static void
encode_and_verify_bcj_filter_props(lzma_vli filter_id)
{
	// First test with start_offset = 0
	// This should result in nothing encoded in
	// the result buffer
	lzma_options_bcj bcj_options = {
		.start_offset = 0
	};

	lzma_filter filter = {
		.id = filter_id,
		.options = (void*) &bcj_options
	};

	uint8_t bcj_properties[4];
	memset(bcj_properties, 0, 4);
	assert_int_equal(lzma_properties_encode(&filter,
				bcj_properties), LZMA_OK);

	assert_int_equal(0, bcj_properties[0]);
	assert_int_equal(0, bcj_properties[1]);
	assert_int_equal(0, bcj_properties[2]);
	assert_int_equal(0, bcj_properties[3]);

	// Test various values as start_offsets to ensure
	// they are encoded properly in the result buffer
	for(uint32_t i = 4; i < UINT32_MAX / 2; i+=1024){
		bcj_options.start_offset = i;
		assert_int_equal(lzma_properties_encode(&filter,
				bcj_properties), LZMA_OK);
		// Combine bcj_properties into a uint32_t
		uint32_t offset = bcj_properties[0];
		offset |= ((uint32_t) (bcj_properties[1])) << 8;
		offset |= ((uint32_t) (bcj_properties[2])) << 16;
		offset |= ((uint32_t) (bcj_properties[3])) << 24;
		assert_int_equal(i, offset);
	}
}


// Test the encoding of properties for each supported filter
// without relying on lzma_properties_decode
// Instead, we compare the result of lzma_properties_encode
// with what is expected in the result buffer
static void
test_lzma_properties_encode(void)
{
#ifdef HAVE_ENCODER_LZMA1
	// Filter properties for LZMA must be 5 bytes
	// The first byte describes the pb, lp, and lc
	// The next 4 bytes encode the dictionary size
	lzma_filter lzma1 = {
		.id = LZMA_FILTER_LZMA1,
		.options = &lzma1_ops
	};

	uint8_t lzma_filter_properties[5];
	assert_int_equal(lzma_properties_encode(&lzma1,
				&lzma_filter_properties), LZMA_OK);

	// The formula for the first byte is:
	// properties = (pb * 5 + lp) * 9 + lc
	uint8_t expected_first_byte = (lzma1_ops.pb * 5 + lzma1_ops.lp)
					* 9 + lzma1_ops.lc;
	assert_int_equal(expected_first_byte, lzma_filter_properties[0]);
	uint32_t dictionary_size = lzma_filter_properties[1];
	dictionary_size |= ((uint32_t) (lzma_filter_properties[2])) << 8;
	dictionary_size |= ((uint32_t) (lzma_filter_properties[3])) << 16;
	dictionary_size |= ((uint32_t) (lzma_filter_properties[4])) << 24;
	assert_int_equal(lzma1_ops.dict_size, dictionary_size);
#endif
#ifdef HAVE_ENCODER_LZMA2
	// Filter properties for LZMA2 must be one byte
	// Bytes 0-5 describe the dictionary size
	// BYtes 6-7 are reserved and MUST be 0
	uint8_t lzma2_filter_properties;
	lzma_filter lzma2 = {
		.id = LZMA_FILTER_LZMA2,
		.options = &lzma2_ops
	};

	assert_int_equal(lzma_properties_encode(&lzma2, &lzma2_filter_properties), LZMA_OK);
	assert_bit_not_set(6, lzma2_filter_properties);
	assert_bit_not_set(7, lzma2_filter_properties);

	// Mantissa is the first bit
	uint8_t mantissa = lzma2_filter_properties & 1 ? 3 : 2;
	// Exponent mask is 111110
	uint8_t exponent = ((lzma2_filter_properties & 0x3E) >> 1) + 11;

	assert_int_equal(lzma2_ops.dict_size, simple_exponentiation(mantissa, exponent));
#endif
#ifdef HAVE_ENCODER_X86
	encode_and_verify_bcj_filter_props(LZMA_FILTER_X86);
#endif
#ifdef HAVE_ENCODER_POWERPC
	encode_and_verify_bcj_filter_props(LZMA_FILTER_POWERPC);
#endif
#ifdef HAVE_ENCODER_IA64
	encode_and_verify_bcj_filter_props(LZMA_FILTER_IA64);
#endif
#ifdef HAVE_ENCODER_ARM
	encode_and_verify_bcj_filter_props(LZMA_FILTER_ARM);
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	encode_and_verify_bcj_filter_props(LZMA_FILTER_ARMTHUMB);
#endif
#ifdef HAVE_ENCODER_SPARC
	encode_and_verify_bcj_filter_props(LZMA_FILTER_SPARC);
#endif
#ifdef HAVE_ENCODER_DELTA
	lzma_options_delta delta_options = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = 1
	};

	lzma_filter delta_filter = {
		.id = LZMA_FILTER_DELTA,
		.options = &delta_options
	};

	for(int i = 1; i < UINT8_MAX; i++){
		uint8_t delta_filter_properties = 0;
		delta_options.dist = i;
		assert_int_equal(lzma_properties_encode(&delta_filter, &delta_filter_properties), LZMA_OK);
		assert_int_equal(i, delta_filter_properties + LZMA_DELTA_DIST_MIN);
	}
#endif
}


// Helper function for test_lzma_properties_decode to test
// all support bcj filters since they all have the same
// properties
static void
decode_and_verify_bcj_filter_props(lzma_vli filter_id)
{
	lzma_options_bcj bcj_options = {
		.start_offset = 0
	};

	lzma_filter filter = {
		.id = filter_id,
		.options = (void*) &bcj_options
	};

	uint8_t bcj_properties[4];
	memset(bcj_properties, 0, 4);
	assert_int_equal(lzma_properties_decode(&filter, NULL,
				bcj_properties, 4), LZMA_OK);
	assert_int_equal(NULL, (int) filter.options);

	for(uint32_t i = 4; i < UINT32_MAX / 2; i+=1024){
		assert_int_equal(lzma_properties_decode(&filter, NULL,
				(uint8_t*) &i, 4), LZMA_OK);
		lzma_options_bcj *result_options = (lzma_options_bcj*) filter.options;
		assert_true(result_options != NULL);
		assert_int_equal(i, result_options->start_offset);
		free(result_options);
	}
}


// Avoid simply using data -> encode -> decode -> data to test
// decode functionality. Instead, computes expected encoded properties
// and decodes it to expected properties
static void
test_lzma_properties_decode(void)
{
#ifdef HAVE_DECODER_LZMA1
	lzma_filter lzma1 = {
		.id = LZMA_FILTER_LZMA1,
		.options = NULL
	};

	uint8_t lzma_filter_properties[5];

	uint8_t pb = 3;
	uint8_t lp = 2;
	uint8_t lc = 1;

	lzma_filter_properties[0] = (pb * 5 + lp) * 9 + lc;
	uint32_t dictionary_size = LZMA_DICT_SIZE_MIN * 4;
	memcpy(lzma_filter_properties + 1, &dictionary_size, sizeof(uint32_t));

	assert_int_equal(lzma_properties_decode(&lzma1, NULL,
				lzma_filter_properties, 5), LZMA_OK);
	assert_true(lzma1.options != NULL);
	lzma_options_lzma *result_options = lzma1.options;

	assert_int_equal(result_options->dict_size, dictionary_size);
	assert_int_equal(result_options->pb, pb);
	assert_int_equal(result_options->lp, lp);
	assert_int_equal(result_options->lc, lc);
	free(result_options);
#endif
#ifdef HAVE_DECODER_LZMA2
	lzma_filter lzma2 = {
		.id = LZMA_FILTER_LZMA2,
		.options = NULL
	};

	// 4 = 000100 (mantissa = 2, exponent = 13)
	// for a dictionary size of 16384 bytes
	uint8_t lzma2_filter_properties = 4;

	assert_int_equal(lzma_properties_decode(&lzma2, NULL, &lzma2_filter_properties, 1), LZMA_OK);
	lzma_options_lzma* lzma2_result_options = lzma2.options;
	assert_int_equal(16384, lzma2_result_options->dict_size);
	free(lzma2_result_options);
#endif
#ifdef HAVE_DECODER_X86
	decode_and_verify_bcj_filter_props(LZMA_FILTER_X86);
#endif
#ifdef HAVE_DECODER_POWERPC
	decode_and_verify_bcj_filter_props(LZMA_FILTER_POWERPC);
#endif
#ifdef HAVE_DECODER_IA64
	decode_and_verify_bcj_filter_props(LZMA_FILTER_IA64);
#endif
#ifdef HAVE_DECODER_ARM
	decode_and_verify_bcj_filter_props(LZMA_FILTER_ARM);
#endif
#ifdef HAVE_DECODER_ARMTHUMB
	decode_and_verify_bcj_filter_props(LZMA_FILTER_ARMTHUMB);
#endif
#ifdef HAVE_DECODER_SPARC
	decode_and_verify_bcj_filter_props(LZMA_FILTER_SPARC);
#endif
#ifdef HAVE_DECODER_DELTA
	lzma_filter delta_filter = {
		.id = LZMA_FILTER_DELTA,
		.options = NULL
	};

	for(int i = 0; i < UINT8_MAX; i++){
		assert_int_equal(lzma_properties_decode(&delta_filter,
						NULL, (uint8_t*) &i, 1),
						LZMA_OK);
		lzma_options_delta *delta_options = delta_filter.options;
		assert_true(delta_options != NULL);
		assert_int_equal(delta_options->dist, i + LZMA_DELTA_DIST_MIN);
	}
#endif
}


void
test_lzma_properties(void)
{
	test_fixture_start();
	run_test(test_lzma_properties_size);
	run_test(test_lzma_properties_encode);
	run_test(test_lzma_properties_decode);
	test_fixture_end();
}
