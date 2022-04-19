///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_filter_flags.c
/// \brief      Tests Filter Flags coders
//
//  Author:     Lasse Collin
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


static void
test_lzma_filter_flags_size(void)
{
	// Loop over all basic filters and verify the size returned from
	// lzma_filter_flags_size is valid
	for(int i = 0; i < sizeof(basic_filters) / sizeof(lzma_filter); i++){
		size_t size = 0;
#ifdef HAVE_ENCODER_LZMA1
		if(basic_filters[i].id == LZMA_FILTER_LZMA1){
			// LZMA 1 is not a valid filter for the xz format
			// LZMA_PROG_ERROR is the required error
			assert_int_equal(lzma_filter_flags_size(&size,
						&basic_filters[i]),
						LZMA_PROG_ERROR);
		}
		else
#endif
		{
			assert_int_equal(lzma_filter_flags_size(&size,
						&basic_filters[i]),
						LZMA_OK);
			assert_true(size != 0 && size != UINT64_MAX);
		}
	}

	// Loop through LZMA_FILTER_MAX_ID_CHECK and verify filter IDs
	// that are invalid return LZMA_OPTIONS_ERROR
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
			assert_int_equal(lzma_filter_flags_size(&size, &invalid_filter),
					LZMA_OPTIONS_ERROR);
		}
	}
}


// Helper function for test_lzma_filter_flags_encode
// Avoid data -> encode -> decode -> compare to data
// Instead create expected encoding and compare to result from
// lzma_filter_flags_encode
// Filter flags for xz are encoded as:
// |Filter ID (VLI)|Size of Properties (VLI)|Filter Properties|
static void
verify_filter_flags_encode(lzma_filter* filter, bool should_encode)
{
	size_t out_pos = 0;
	uint32_t size = 0;
	lzma_vli filter_id = 0, size_of_properties = 0;
	size_t filter_id_vli_size = 0, size_of_properties_vil_size = 0;

	// First calculate the size of filter flags to know how much
	// memory to allocate to hold the filter flags encoded
	assert_int_equal(lzma_filter_flags_size(&size, filter), LZMA_OK);
	uint8_t* encoded_out = (uint8_t*) malloc(size * sizeof(uint8_t));
	assert_true(encoded_out != NULL);
	if(!should_encode) {
		assert_false(lzma_filter_flags_encode(filter, encoded_out,
				&out_pos, size) == LZMA_OK);
		return;
	}
	// Next encode the filter flags for the provided filter
	assert_int_equal(lzma_filter_flags_encode(filter, encoded_out,
				&out_pos, size), LZMA_OK);
	assert_int_equal(size, out_pos);
	// Next decode the vli for the filter ID and verify it matches
	// the expected filter id
	assert_int_equal(lzma_vli_decode(&filter_id, NULL, encoded_out,
				&filter_id_vli_size, size), LZMA_OK);
	assert_ulong_equal(filter->id, filter_id);

	// Next decode the size of properites and ensure it equals
	// the expected size
	// Expected size should be:
	// total filter flag length - size of filter id VLI + size of
	//                            property size VLI
	assert_int_equal(lzma_vli_decode(&size_of_properties, NULL,
				encoded_out + filter_id_vli_size,
				&size_of_properties_vil_size, size),
				LZMA_OK);
	assert_ulong_equal(size - (size_of_properties_vil_size
				+ filter_id_vli_size), size_of_properties);
	// Not verifying the contents of Filter Properties since
	// there is a different test for that already
	free(encoded_out);
}


static void
test_lzma_filter_flags_encode(void)
{
	lzma_options_bcj bcj_ops_no_offset = {
		.start_offset = 0
	};

	lzma_options_bcj bcj_ops_with_offset = {
		.start_offset = 32
	};

	// No test for LZMA1 since the xz format does not support LZMA1
	// and so the flags cannot be encoded for that filter
#ifdef HAVE_ENCODER_LZMA2
	lzma_filter lzma2 = {
		.id = LZMA_FILTER_LZMA2,
		.options = &lzma2_ops
	};
	// Test with predefined options that should pass
	verify_filter_flags_encode(&lzma2, true);
	// Test with NULL options that should fail
	lzma2.options = NULL;
	//verify_filter_flags_encode(&lzma2, false);
#endif
#ifdef HAVE_ENCODER_X86
	lzma_filter x86 = {
		.id = LZMA_FILTER_X86,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&x86, true);
	x86.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&x86, true);
	// NULL options should pass for bcj filters
	x86.options = NULL;
	verify_filter_flags_encode(&x86, true);
#endif
#ifdef HAVE_ENCODER_POWERPC
	lzma_filter power_pc = {
		.id = LZMA_FILTER_POWERPC,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&power_pc, true);
	power_pc.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&power_pc, true);
	power_pc.options = NULL;
	verify_filter_flags_encode(&power_pc, true);
#endif
#ifdef HAVE_ENCODER_IA64
	lzma_filter ia64 = {
		.id = LZMA_FILTER_IA64,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&ia64, true);
	ia64.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&ia64, true);
	ia64.options = NULL;
	verify_filter_flags_encode(&ia64, true);
#endif
#ifdef HAVE_ENCODER_ARM
	lzma_filter arm = {
		.id = LZMA_FILTER_ARM,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&arm, true);
	arm.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&arm, true);
	arm.options = NULL;
	verify_filter_flags_encode(&arm, true);
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	lzma_filter arm_thumb = {
		.id = LZMA_FILTER_ARMTHUMB,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&arm_thumb, true);
	arm_thumb.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&arm_thumb, true);
	arm_thumb.options = NULL;
	verify_filter_flags_encode(&arm_thumb, true);
#endif
#ifdef HAVE_ENCODER_SPARC
	lzma_filter sparc = {
		.id = LZMA_FILTER_SPARC,
		.options = &bcj_ops_no_offset
	};

	verify_filter_flags_encode(&sparc, true);
	sparc.options = &bcj_ops_with_offset;
	verify_filter_flags_encode(&sparc, true);
	sparc.options = NULL;
	verify_filter_flags_encode(&sparc, true);
#endif
#ifdef HAVE_ENCODER_DELTA
	lzma_options_delta delta_ops = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MAX
	};

	lzma_options_delta delta_ops_below_min = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MIN - 1
	};

	lzma_options_delta delta_ops_above_max = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MAX + 1
	};

	lzma_filter delta = {
		.id = LZMA_FILTER_DELTA,
		.options = &delta_ops
	};

	verify_filter_flags_encode(&delta, true);

	// Next test error case using minimum - 1 delta distance
	delta.options = &delta_ops_below_min;
	verify_filter_flags_encode(&delta, false);

	// Next test error case using maximum + 1 delta distance
	delta.options = &delta_ops_above_max;
	verify_filter_flags_encode(&delta, false);

	// Next test null case
	delta.options = NULL;
	verify_filter_flags_encode(&delta, false);
#endif
}


// Helper function for test_lzma_filter_flags_decode
// Encodes the filter_in without using lzma_filter_flags_encode
// Leaves the specific assertions of filter_out options to the caller
// because it is agnostic to the type of options used in the call
static void
verify_filter_flags_decode(lzma_filter* filter_in, lzma_filter* filter_out)
{
	uint32_t properties_size = 0, total_size = 0;
	size_t out_pos = 0, in_pos = 0;

	assert_int_equal(lzma_filter_flags_size(&total_size, filter_in),
				LZMA_OK);
	uint8_t *filter_flag_buffer = (uint8_t*) malloc(total_size);
	assert_true(filter_flag_buffer != NULL);

	assert_int_equal(lzma_properties_size(&properties_size, filter_in),
				LZMA_OK);
	assert_int_equal(lzma_vli_encode(filter_in->id, NULL,
				filter_flag_buffer, &out_pos,
				total_size), LZMA_OK);
	assert_int_equal(lzma_vli_encode(properties_size, NULL,
				filter_flag_buffer, &out_pos,
				total_size), LZMA_OK);
	assert_int_equal(lzma_properties_encode(filter_in,
				filter_flag_buffer + out_pos), LZMA_OK);
	assert_int_equal(lzma_filter_flags_decode(filter_out, NULL,
			filter_flag_buffer, &in_pos, total_size),
			LZMA_OK);
	assert_ulong_equal(filter_in->id, filter_out->id);

	free(filter_flag_buffer);
}


// Similar to test_lzma_properties_decode, will avoid
// data -> encode -> decode -> data to verify correctness
// of lzma_filter_flags_decode
static void
test_lzma_filter_flags_decode(void)
{
	lzma_options_bcj bcj_ops_no_offset = {
		.start_offset = 0
	};

	lzma_options_bcj bcj_ops_with_offset = {
		.start_offset = 32
	};

#ifdef HAVE_DECODER_LZMA2
	lzma_filter lzma2 = {
		.id = LZMA_FILTER_LZMA2,
		.options = &lzma2_ops
	};

	lzma_filter lzma2_decoded = {
		.id = LZMA_FILTER_LZMA2,
		.options = NULL
	};

	verify_filter_flags_decode(&lzma2, &lzma2_decoded);
	lzma_options_lzma* decoded_options_lzma2 = lzma2_decoded.options;
	assert_int_equal(lzma2_ops.dict_size,
				decoded_options_lzma2->dict_size);
	free(decoded_options_lzma2);
#endif
#ifdef HAVE_DECODER_X86
	lzma_filter x86 = {
		.id = LZMA_FILTER_X86,
		.options = &bcj_ops_no_offset
	};

	lzma_filter x86_decoded = {
		.id = LZMA_FILTER_X86,
		.options = NULL
	};

	verify_filter_flags_decode(&x86, &x86_decoded);
	// No offset so decoded options will be NULL
	assert_true(x86_decoded.options == NULL);

	x86.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&x86, &x86_decoded);
	lzma_options_bcj* decoded_options_x86 = x86_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_x86->start_offset);
	free(decoded_options_x86);
#endif
#ifdef HAVE_DECODER_POWERPC
	lzma_filter power_pc = {
		.id = LZMA_FILTER_POWERPC,
		.options = &bcj_ops_no_offset
	};

	lzma_filter power_pc_decoded = {
		.id = LZMA_FILTER_POWERPC,
		.options = NULL
	};

	verify_filter_flags_decode(&power_pc, &power_pc_decoded);
	assert_true(power_pc_decoded.options == NULL);

	power_pc.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&power_pc, &power_pc_decoded);
	lzma_options_bcj* decoded_options_power_pc = power_pc_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_power_pc->start_offset);
	free(decoded_options_power_pc);
#endif
#ifdef HAVE_DECODER_IA64
	lzma_filter ia64 = {
		.id = LZMA_FILTER_IA64,
		.options = &bcj_ops_no_offset
	};

	lzma_filter ia64_decoded = {
		.id = LZMA_FILTER_IA64,
		.options = NULL
	};

	verify_filter_flags_decode(&ia64, &ia64_decoded);
	assert_true(ia64_decoded.options == NULL);

	ia64.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&ia64, &ia64_decoded);
	lzma_options_bcj* decoded_options_ia64 = ia64_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_ia64->start_offset);
	free(decoded_options_ia64);
#endif
#ifdef HAVE_DECODER_ARM
	lzma_filter arm = {
		.id = LZMA_FILTER_ARM,
		.options = &bcj_ops_no_offset
	};

	lzma_filter arm_decoded = {
		.id = LZMA_FILTER_ARM,
		.options = NULL
	};

	verify_filter_flags_decode(&arm, &arm_decoded);
	assert_true(arm_decoded.options == NULL);

	arm.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&arm, &arm_decoded);
	lzma_options_bcj* decoded_options_arm = arm_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_arm->start_offset);
	free(decoded_options_arm);
#endif
#ifdef HAVE_DECODER_ARMTHUMB
	lzma_filter arm_thumb = {
		.id = LZMA_FILTER_ARMTHUMB,
		.options = &bcj_ops_no_offset
	};

	lzma_filter arm_thumb_decoded = {
		.id = LZMA_FILTER_ARMTHUMB,
		.options = NULL
	};

	verify_filter_flags_decode(&arm_thumb, &arm_thumb_decoded);
	assert_true(arm_thumb_decoded.options == NULL);

	arm_thumb.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&arm_thumb, &arm_thumb_decoded);
	lzma_options_bcj* decoded_options_arm_thumb = arm_thumb_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_arm_thumb->start_offset);
	free(decoded_options_arm_thumb);
#endif
#ifdef HAVE_DECODER_SPARC
	lzma_filter sparc = {
		.id = LZMA_FILTER_SPARC,
		.options = &bcj_ops_no_offset
	};

	lzma_filter sparc_decoded = {
		.id = LZMA_FILTER_SPARC,
		.options = NULL
	};

	verify_filter_flags_decode(&sparc, &sparc_decoded);
	assert_true(sparc_decoded.options == NULL);

	sparc.options = &bcj_ops_with_offset;
	verify_filter_flags_decode(&sparc, &sparc_decoded);
	lzma_options_bcj* decoded_options_sparc = sparc_decoded.options;
	assert_int_equal(bcj_ops_with_offset.start_offset,
				decoded_options_sparc->start_offset);
	free(decoded_options_sparc);
#endif
#ifdef HAVE_DECODER_DELTA
	lzma_options_delta delta_ops_max = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MAX
	};

	lzma_options_delta delta_ops_min = {
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MIN
	};

	lzma_filter delta = {
		.id = LZMA_FILTER_DELTA,
		.options = &delta_ops_max
	};

	lzma_filter delta_decoded = {
		.id = LZMA_FILTER_DELTA,
		.options = NULL
	};

	// First test using max delta distance
	verify_filter_flags_decode(&delta, &delta_decoded);
	lzma_options_delta* decoded_options_delta = delta_decoded.options;
	assert_int_equal(LZMA_DELTA_DIST_MAX, decoded_options_delta->dist);
	free(decoded_options_delta);

	// Next test using minimum delta distance
	delta.options = &delta_ops_min;
	delta_decoded.options = NULL;
	verify_filter_flags_decode(&delta, &delta_decoded);
	decoded_options_delta = delta_decoded.options;
	assert_int_equal(LZMA_DELTA_DIST_MIN, decoded_options_delta->dist);
	free(decoded_options_delta);
#endif
}


void
test_filter_flags(void)
{
	test_fixture_start();
	run_test(test_lzma_filter_flags_size);
	run_test(test_lzma_filter_flags_encode);
	run_test(test_lzma_filter_flags_decode);
	test_fixture_end();
}
