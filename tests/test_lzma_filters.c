///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_lzma_filters.c
/// \brief      Tests general liblzma filter API functions
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "test_utils.h"
#include "config.h"
#include "test_lzma_filter_utils.h"
#include "sysdefs.h"
#include <stdlib.h>

#define OUTBUF_SIZE 4096
// Will use OUTBUF_SIZE / 4 size blocks
#define BLOCK_SIZE OUTBUF_SIZE / 4

// Globals used by test_lzma_filters_update helper functions
static lzma_options_delta delta_updated = {
		.dist = 100
};

// Values filled in by test_lzma_filters_update
static lzma_options_lzma lzma2_updated;

// Update filters to use delta -> LZMA2
static lzma_filter updated_filters[3] = {
	{
		.id = LZMA_FILTER_DELTA,
		.options = &delta_updated
	},
	{
		.id = LZMA_FILTER_LZMA2,
		.options = &lzma2_updated
	},
	{
		.id = LZMA_VLI_UNKNOWN,
		.options = NULL
	}
};


static void
test_lzma_filter_encoder_is_supported(void)
{
	const lzma_vli supported_encoders[] = {
#ifdef HAVE_ENCODER_LZMA1
	LZMA_FILTER_LZMA1,
#endif
#ifdef HAVE_ENCODER_LZMA2
	LZMA_FILTER_LZMA2,
#endif
#ifdef HAVE_ENCODER_X86
	LZMA_FILTER_X86,
#endif
#ifdef HAVE_ENCODER_POWERPC
	LZMA_FILTER_POWERPC,
#endif
#ifdef HAVE_ENCODER_IA64
	LZMA_FILTER_IA64,
#endif
#ifdef HAVE_ENCODER_ARM
	LZMA_FILTER_ARM,
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	LZMA_FILTER_ARMTHUMB,
#endif
#ifdef HAVE_ENCODER_SPARC
	LZMA_FILTER_SPARC,
#endif
#ifdef HAVE_ENCODER_DELTA
	LZMA_FILTER_DELTA,
#endif
	};

	// Test all supported encoders are
	// validated by lzma_filter_encoder_is_supported
	for(int i = 0; i < sizeof(supported_encoders) / sizeof(lzma_vli);
								 i++) {
		assert_true(lzma_filter_encoder_is_supported(supported_encoders[i]));
	}

	// Test invalid encoders are rejected
	for(int i = 0; i < LZMA_FILTER_MAX_ID_CHECK; i++) {
		int supported = 0;
		for(int j = 0; j < sizeof(supported_encoders) /
						sizeof(lzma_vli); j++) {
			if(i == supported_encoders[j]) {
				supported = 1;
				break;
			}
		}
		if(!supported) {
			assert_false(lzma_filter_encoder_is_supported(i));
		}
	}
}


static void
test_lzma_filter_decoder_is_supported(void)
{
	const lzma_vli supported_decoders[] = {
#ifdef HAVE_DECODER_LZMA1
	LZMA_FILTER_LZMA1,
#endif
#ifdef HAVE_DECODER_LZMA2
	LZMA_FILTER_LZMA2,
#endif
#ifdef HAVE_DECODER_X86
	LZMA_FILTER_X86,
#endif
#ifdef HAVE_DECODER_POWERPC
	LZMA_FILTER_POWERPC,
#endif
#ifdef HAVE_DECODER_IA64
	LZMA_FILTER_IA64,
#endif
#ifdef HAVE_DECODER_ARM
	LZMA_FILTER_ARM,
#endif
#ifdef HAVE_DECODER_ARMTHUMB
	LZMA_FILTER_ARMTHUMB,
#endif
#ifdef HAVE_DECODER_SPARC
	LZMA_FILTER_SPARC,
#endif
#ifdef HAVE_DECODER_DELTA
	LZMA_FILTER_DELTA,
#endif
	};

	// Test all supported encoders are
	// validated by lzma_filter_encoder_is_supported
	for(int i = 0; i < sizeof(supported_decoders) / sizeof(lzma_vli);
								i++) {
		assert_true(lzma_filter_decoder_is_supported(
				supported_decoders[i]));
	}

	// Test invalid encoders are rejected
	for(int i = 0; i < LZMA_FILTER_MAX_ID_CHECK; i++){
		int supported = 0;
		for(int j = 0; j < sizeof(supported_decoders) /
					sizeof(lzma_vli); j++) {
			if(i == supported_decoders[j]) {
				supported = 1;
				break;
			}
		}
		if(!supported) {
			assert_false(lzma_filter_decoder_is_supported(i));
		}
	}
}


static void
test_lzma_filters_copy(void)
{
	// Test for correct errors with NULL src and / or dest
	assert_int_equal(lzma_filters_copy(NULL, NULL, NULL),
				LZMA_PROG_ERROR);
	assert_int_equal(lzma_filters_copy(lzma1_filters, NULL, NULL),
				LZMA_PROG_ERROR);
	assert_int_equal(lzma_filters_copy(NULL, lzma2_filters, NULL),
				LZMA_PROG_ERROR);

#ifdef TEST_FILTER_CHAIN_LZMA1
	// LZMA1 filter chain consists of X86 -> delta -> LZMA1 -> NULL
	lzma_filter lzma1_copy[4];
	// Copy lzma1 filters
	assert_int_equal(lzma_filters_copy(lzma1_filters, lzma1_copy,
						NULL), LZMA_OK);
	// Verify lzma1 filter ids have been copied correctly
	for(int i = 0; i < 4; i++) {
		assert_int_equal(lzma1_filters[i].id, lzma1_copy[i].id);
	}
	// Verify lzma1 filter options have been copied correctly
	lzma_options_bcj* lzma1_copy_bcj_options = (lzma_options_bcj*) lzma1_copy[0].options;
	assert_n_array_equal(((char*) &bcj_ops_lzma1), ((char*) lzma1_copy_bcj_options), sizeof(lzma_options_bcj));

	lzma_options_delta* lzma1_copy_delta_options = (lzma_options_delta*) lzma1_copy[1].options;
	assert_n_array_equal(((char*) &delta_ops_lzma1), ((char*) lzma1_copy_delta_options), sizeof(lzma_options_bcj));

	lzma_options_lzma* lzma1_copy_lzma_options = (lzma_options_lzma*) lzma1_copy[2].options;
	assert_n_array_equal(((char*) &lzma1_ops), ((char*) lzma1_copy_lzma_options), sizeof(lzma_options_bcj));
#endif
#ifdef TEST_FILTER_CHAIN_LZMA2
	// LZMA2 filter chain consists of ARM -> delta -> LZMA2 -> NULL
	lzma_filter lzma2_copy[4];
	// Copy lzma2 filters
	assert_int_equal(lzma_filters_copy(lzma2_filters, lzma2_copy,
						NULL), LZMA_OK);
	// Verify lzma2 filter ids have been copied correctly
	for(int i = 0; i < 4; i++){
		assert_int_equal(lzma2_filters[i].id, lzma2_copy[i].id);
	}
	// Verify lzma2 filter options have been copied correctly
	lzma_options_bcj* lzma2_copy_bcj_options = (lzma_options_bcj*) lzma2_copy[0].options;
	assert_n_array_equal(((char*) &bcj_ops_lzma2), ((char*) lzma2_copy_bcj_options), sizeof(lzma_options_bcj));

	lzma_options_delta* lzma2_copy_delta_options = (lzma_options_delta*) lzma2_copy[1].options;
	assert_n_array_equal(((char*) &delta_ops_lzma2), ((char*) lzma2_copy_delta_options), sizeof(lzma_options_bcj));

	lzma_options_lzma* lzma2_copy_lzma_options = (lzma_options_lzma*) lzma2_copy[2].options;
	assert_n_array_equal(((char*) &lzma2_ops), ((char*) lzma2_copy_lzma_options), sizeof(lzma_options_bcj));
#endif
#ifdef TEST_FILTER_CHAIN_INVALID
	// Should be invalid because chain is LZMA2 -> delta
	lzma_filter invalid_copy[2];

	// Copy invalid filters
	assert_int_equal(lzma_filters_copy(invalid_filters, invalid_copy,
				NULL), LZMA_OPTIONS_ERROR);
#endif
}


#ifdef HAVE_ENCODER_LZMA2
// Helper function for test_lzma_filters_update
// Encodes a single block and then issues a LZMA_FULL_FLUSH action
static void
encode_block_full_flush(lzma_stream* strm)
{
	strm->avail_in = BLOCK_SIZE;
	while(true) {
		lzma_ret ret = lzma_code(strm, LZMA_RUN);
		if(strm->avail_in == 0 || ret == LZMA_STREAM_END) {
			// Finish block and break
			assert_int_equal(lzma_code(strm,
					LZMA_FULL_FLUSH), LZMA_STREAM_END);
			break;
		}
		else {
			assert_int_equal(LZMA_OK, ret);
		}
	}
}

// Helper function for test_lzma_filters_update
// Decodes as much as possible from the stream
// until the stream ends
static void
decode_partial_strm(lzma_stream* strm)
{
	lzma_action action = LZMA_RUN;
	while(true) {
		lzma_ret ret = lzma_code(strm, action);
		if(strm->avail_in == 0 && action == LZMA_RUN) {
			action = LZMA_FINISH;
		}
		else if(ret == LZMA_STREAM_END || ret == LZMA_BUF_ERROR) {
			return;
		}
		else {
			assert_int_equal(LZMA_OK, ret);
		}
	}
}

// Helper function for test_lzma_filters_update
// Encodes a single block and then issues a LZMA_SYNC_FLUSH action
static void
encode_block_sync_flush(lzma_stream* strm)
{
	strm->avail_in = BLOCK_SIZE;
	while(true) {
		lzma_ret ret = lzma_code(strm, LZMA_RUN);
		if(strm->avail_in == 0 || ret == LZMA_STREAM_END) {
			// Finish block and break
			assert_int_equal(lzma_code(strm,
					LZMA_SYNC_FLUSH), LZMA_STREAM_END);
			break;
		}
		else {
			assert_int_equal(LZMA_OK, ret);
		}
	}
}

// Helper function for test_lzma_filters_update
// Reads the bytes from the block header and validates
// the list of filters are included in the header
static void
validate_block_header(uint8_t* hdr, lzma_filter* filters)
{
	// Get header size
	size_t pos = 0;
	size_t header_size = (hdr[pos++] + 1) * 4;
	// Get block flags
	uint8_t block_flags = hdr[pos++];
	// Compressed single threaded, so size fields should not be present
	assert_bit_not_set(6, block_flags);
	assert_bit_not_set(7, block_flags);
	// Store number of filters now (check value later)
	uint8_t number_of_filters = (block_flags & 0x03) + 1;

	// Ensure reserved bits are empty
	assert_int_equal(0, block_flags & 0x3C);
	// Check all three filters
	// These are encoded as:
	// |Filter ID|Size of Properities|Filter Properties|
	int filter_index = 0;
	lzma_filter filter = filters[filter_index];
	while(filter.id != LZMA_VLI_UNKNOWN && pos < header_size) {
		// Decode Filter ID
		lzma_vli filter_id = 0;
		assert_int_equal(lzma_vli_decode(&filter_id, NULL, hdr,
					&pos, header_size), LZMA_OK);
		assert_ulong_equal(filter.id, filter_id);
		// Decode Size of Properties
		lzma_vli prop_size = 0;
		assert_int_equal(lzma_vli_decode(&prop_size, NULL, hdr,
					&pos, header_size), LZMA_OK);
		// Skip past the actual properties to the next header
		pos += prop_size;
		filter = filters[++filter_index];
	}
	// Add padding
	pos += pos % 4;
	// Add size of CRC32 check
	pos += 4;
	assert_int_equal(header_size, pos);
	// Now that we know the expected filter count, ensure it matches
	// the value in the block header
	assert_int_equal(filter_index, number_of_filters);
}

// The lzma_filters_update works in three cases:
// 1. When using stream encoder, execute a LZMA_FULL_FLUSH
//    then set a new filter chain for the next block
// 2. When using raw, block, or stream encoder LZMA_SYNC_FLUSH
//    can be used to change the filter-specific options in
//    the middle of an encoding
// 3. When no data has been compressed yet, it allows changing
//    the filters or filter options

// Helper function for test_lzma_filters_update
// Tests case 1
static void
test_mid_stream_filter_change(const uint8_t* input_data)
{
	// First setup a stream encoder for LZMA2
	lzma_stream strm = LZMA_STREAM_INIT;
	assert_int_equal(lzma_stream_encoder(&strm, lzma2_filters,
				LZMA_CHECK_CRC64), LZMA_OK);

	// Will not compress more than OUTBUF_SIZE total
	uint8_t output_data[OUTBUF_SIZE];

	strm.avail_out = OUTBUF_SIZE;
	strm.next_in = input_data;
	strm.next_out = output_data;

	// Encode first block
	encode_block_full_flush(&strm);

	assert_int_equal(lzma_filters_update(&strm, updated_filters),
				 LZMA_OK);

	// Encode the next block
	uint8_t* second_block_start = strm.next_out;
	encode_block_full_flush(&strm);

	// Update filters back to original
	assert_int_equal(lzma_filters_update(&strm, lzma2_filters),
				 LZMA_OK);

	// Encode the last block
	uint8_t* last_block_start = strm.next_out;
	encode_block_full_flush(&strm);

	// Next, verify successful output by stream decompressing
	// and comparing to original input
	uint8_t* decompressed_orig = (uint8_t*) malloc(strm.total_in);

	lzma_stream decode_strm = LZMA_STREAM_INIT;
	assert_int_equal(lzma_stream_decoder(&decode_strm, UINT64_MAX,
					LZMA_TELL_NO_CHECK), LZMA_OK);
	decode_strm.avail_in = strm.total_out;
	decode_strm.avail_out = strm.total_in;
	decode_strm.next_in = output_data;
	decode_strm.next_out = decompressed_orig;

	decode_partial_strm(&decode_strm);
	assert_n_array_equal(input_data, decompressed_orig,
				decode_strm.total_out);

	// Next, verify the block headers were successfully changed
	// by inspecting the bytes of the compressed buffer
	// First, skip past stream header
	uint8_t* first_block_start = output_data + LZMA_STREAM_HEADER_SIZE;
	validate_block_header(first_block_start, lzma2_filters);
	validate_block_header(second_block_start, updated_filters);
	validate_block_header(last_block_start, lzma2_filters);

	free(decompressed_orig);
}


// Helper function for test_lzma_filters_update
// Tests case 2
static void
test_mid_stream_filter_update(const uint8_t* input_data)
{
	// First define a set of altered filter options
	// Cannot use any of the bcj filters because they do not support
	// LZMA_SYNC_FLUSH
	lzma_filter original_filters[3] = {
		{
			.id = LZMA_FILTER_DELTA,
			.options = &delta_ops_lzma2
		},
		{
			.id = LZMA_FILTER_LZMA2,
			.options = &lzma2_ops
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};

	// Will not compress more than OUTBUF_SIZE total
	uint8_t output_data[OUTBUF_SIZE];

	// Next set up LZMA2 raw encoder
	lzma_stream lzma2_raw_strm = LZMA_STREAM_INIT;
	assert_int_equal(lzma_raw_encoder(&lzma2_raw_strm, original_filters),
						LZMA_OK);
	lzma2_raw_strm.avail_out = OUTBUF_SIZE;
	lzma2_raw_strm.next_in = input_data;
	lzma2_raw_strm.next_out = output_data;
	// Clear so we can reuse the output buffer
	encode_block_sync_flush(&lzma2_raw_strm);

	lzma_filter altered_filters[3];
	assert_int_equal(lzma_filters_copy(original_filters,
				altered_filters, NULL), LZMA_OK);

	// Update just the delta filter
	altered_filters[0].options = &delta_updated;

	assert_int_equal(lzma_filters_update(&lzma2_raw_strm,
					altered_filters), LZMA_OK);
	// Encode the next block with updated filters
	encode_block_sync_flush(&lzma2_raw_strm);
	lzma_stream raw_decode_strm = LZMA_STREAM_INIT;
	uint8_t* decompressed_orig = (uint8_t*) malloc(lzma2_raw_strm.total_in);

	assert_int_equal(lzma_raw_decoder(&raw_decode_strm, original_filters),
						LZMA_OK);
	raw_decode_strm.avail_in = lzma2_raw_strm.total_out;
	raw_decode_strm.avail_out = lzma2_raw_strm.total_in;
	raw_decode_strm.next_in = output_data;
	raw_decode_strm.next_out = decompressed_orig;

	decode_partial_strm(&raw_decode_strm);
	assert_n_array_equal(input_data, decompressed_orig,
				raw_decode_strm.total_out);
	free(decompressed_orig);

	// Next set up stream encoder
	lzma_stream lzma2_stream_strm = LZMA_STREAM_INIT;
	assert_int_equal(lzma_stream_encoder(&lzma2_stream_strm,
			original_filters, LZMA_CHECK_CRC64), LZMA_OK);
	lzma2_stream_strm.avail_out = OUTBUF_SIZE;
	lzma2_stream_strm.next_in = input_data;
	lzma2_stream_strm.next_out = output_data;
	// Clear so we can reuse the output buffer
	memzero(output_data, OUTBUF_SIZE);
	encode_block_sync_flush(&lzma2_stream_strm);

	assert_int_equal(lzma_filters_update(&lzma2_stream_strm,
					altered_filters), LZMA_OK);
	encode_block_sync_flush(&lzma2_stream_strm);
	lzma_stream lzma2_decode_strm = LZMA_STREAM_INIT;
	decompressed_orig = (uint8_t*) malloc(lzma2_stream_strm.total_in);

	assert_int_equal(lzma_stream_decoder(&lzma2_decode_strm, UINT64_MAX,
					LZMA_TELL_NO_CHECK), LZMA_OK);

	lzma2_decode_strm.avail_in = lzma2_stream_strm.total_out;
	lzma2_decode_strm.avail_out = lzma2_stream_strm.total_in;
	lzma2_decode_strm.next_in = output_data;
	lzma2_decode_strm.next_out = decompressed_orig;

	decode_partial_strm(&lzma2_decode_strm);
	assert_n_array_equal(input_data, decompressed_orig,
				lzma2_decode_strm.total_out);
	free(decompressed_orig);
}


// Helper function for test_lzma_filters_update
// Tests case 3
static void
test_pre_compression_filter_change(const uint8_t* input_data)
{
	// First set up an LZMA2 stream encoder
	lzma_stream strm = LZMA_STREAM_INIT;
	// Set the filters to the default lzma_filters
	assert_int_equal(lzma_stream_encoder(&strm, lzma2_filters,
				LZMA_CHECK_CRC64), LZMA_OK);

	// Will not compress more than OUTBUF_SIZE total
	uint8_t output_data[OUTBUF_SIZE];

	strm.avail_out = OUTBUF_SIZE;
	strm.next_in = input_data;
	strm.next_out = output_data;
	// Then, update the filters
	assert_int_equal(lzma_filters_update(&strm, updated_filters),
						LZMA_OK);
	// Then compress a block of input
	encode_block_full_flush(&strm);
	// Check the output to ensure it has the expected updated filters
	uint8_t *first_block_start = output_data + LZMA_STREAM_HEADER_SIZE;
	validate_block_header(first_block_start, updated_filters);
}
#endif


static void
test_lzma_filters_update(void)
{
#ifdef HAVE_ENCODER_LZMA2
	// Read input text data
	uint8_t *input_data = NULL;
	size_t input_data_size = read_file_into_buffer(
				"files/lzma_filters/raw_original.txt",
				&input_data);
	assert_true(input_data_size > 0);

	// Keep all options from lzma2_ops the same except
	// dictionary size
	lzma2_updated.dict_size = LZMA_DICT_SIZE_MIN;
	lzma2_updated.preset_dict = lzma2_ops.preset_dict;
	lzma2_updated.preset_dict_size = lzma2_ops.dict_size;
	lzma2_updated.lc = lzma2_ops.lc;
	lzma2_updated.lp = lzma2_ops.lp;
	lzma2_updated.pb = lzma2_ops.pb;
	lzma2_updated.mode = lzma2_ops.mode;
	lzma2_updated.nice_len = lzma2_ops.nice_len;
	lzma2_updated.mf = lzma2_ops.mf;
	lzma2_updated.depth = lzma2_ops.depth;

	test_mid_stream_filter_change(input_data);
	test_mid_stream_filter_update(input_data);
	test_pre_compression_filter_change(input_data);

	free(input_data);
#endif
}


void
test_lzma_filters(void)
{
	test_fixture_start();
	run_test(test_lzma_filter_encoder_is_supported);
	run_test(test_lzma_filter_decoder_is_supported);
	run_test(test_lzma_filters_copy);
	run_test(test_lzma_filters_update);
	test_fixture_end();
}
