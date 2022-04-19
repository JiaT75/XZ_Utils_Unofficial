///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_lzma_filter_str_conversion.c
/// \brief      Tests converting filter chains to and from strings
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "test_lzma_filter_utils.h"


const char expected_lzma1_filters_str[] = "x86+delta+lzma1=lc:0,pb:0," \
		"mode:fast,nice:32,mf:hc3,depth:0";
const char expected_lzma2_filters_str[] = "arm=start_offset:16+delta=" \
		"dist:256+lzma2=lc:4,pb:4,mode:normal,nice:273,mf:hc4," \
		"depth:200";


static void
test_filter_to_str_expect_pass(void)
{
	char result[150];
#ifdef TEST_FILTER_CHAIN_LZMA1
	assert_int_equal(LZMA_OK, lzma_filters_to_str(lzma1_filters,
			result, sizeof(expected_lzma1_filters_str)));
	assert_string_equal(expected_lzma1_filters_str, result);

	lzma_filter delta_alone[2] = {
		{
			.id = LZMA_FILTER_DELTA,
			.options = &delta_ops_lzma1
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};

	assert_int_equal(LZMA_OK, lzma_filters_to_str(delta_alone,
			result, sizeof("delta")));
	assert_string_equal("delta", result);
#endif
#ifdef TEST_FILTER_CHAIN_LZMA2
	assert_int_equal(LZMA_OK, lzma_filters_to_str(lzma2_filters,
			result, sizeof(expected_lzma2_filters_str)));
	assert_string_equal(expected_lzma2_filters_str, result);

	lzma_filter arm_alone[2] = {
		{
			.id = LZMA_FILTER_ARM,
			.options = &bcj_ops_lzma2
		},
		{
			.id = LZMA_VLI_UNKNOWN,
			.options = NULL
		}
	};

	const char expect_arm_str[] = "arm=start_offset:16";
	assert_int_equal(LZMA_OK, lzma_filters_to_str(arm_alone,
			result, sizeof(expect_arm_str)));
	assert_string_equal(expect_arm_str, result);
#endif
}


static void
test_filter_to_str_expect_fail(void)
{
	char result[150];
#ifdef TEST_FILTER_CHAIN_LZMA1
	// Test if the buffer is too small (should return LZMA_BUF_ERROR)
	assert_int_equal(LZMA_BUF_ERROR, lzma_filters_to_str(
			lzma1_filters, result, 0));
	assert_int_equal(LZMA_BUF_ERROR, lzma_filters_to_str(
			lzma1_filters, result, 10));
	assert_int_equal(LZMA_BUF_ERROR, lzma_filters_to_str(
			lzma1_filters, result,
			sizeof(expected_lzma1_filters_str) - 1));

	// Test with null arguments
	assert_int_equal(LZMA_PROG_ERROR, lzma_filters_to_str(
			lzma1_filters, NULL,
			sizeof(expected_lzma1_filters_str)));
	assert_int_equal(LZMA_PROG_ERROR, lzma_filters_to_str(
			NULL, result,
			sizeof(expected_lzma1_filters_str)));
#endif
}


static void
compare_lzma_filters(lzma_options_lzma *expected, lzma_options_lzma *actual)
{
	assert_true(expected != NULL);
	assert_true(actual != NULL);
	assert_int_equal(expected->dict_size, actual->dict_size);
	assert_int_equal(expected->preset_dict, actual->preset_dict);
	assert_int_equal(expected->preset_dict_size,
			actual->preset_dict_size);
	assert_int_equal(expected->lc, actual->lc);
	assert_int_equal(expected->lp, actual->lp);
	assert_int_equal(expected->pb, actual->pb);
	assert_int_equal(expected->mode, actual->mode);
	assert_int_equal(expected->nice_len, actual->nice_len);
	assert_int_equal(expected->mf, actual->mf);
	assert_int_equal(expected->depth, actual->depth);
}


static void
test_lzma2_preset_match(uint32_t preset)
{
	char preset_str[] = {'l', 'z', 'm', 'a', '2', '=', preset + 48, 0};
	lzma_filter filters[2];
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			filters, NULL,
			preset_str));
	lzma_options_lzma expected_preset;
	assert_false(lzma_lzma_preset(&expected_preset, preset));
	assert_ulong_equal(LZMA_FILTER_LZMA2, filters[0].id);
	compare_lzma_filters(&expected_preset,
			filters[0].options);
	assert_ulong_equal(LZMA_VLI_UNKNOWN, filters[1].id);
}

static void
test_str_to_filter_expect_pass(void)
{
#ifdef TEST_FILTER_CHAIN_LZMA1
	lzma_filter lzma1_test_filters[LZMA_FILTERS_MAX + 1];
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma1_test_filters, NULL,
			expected_lzma1_filters_str));
	// Test BCJ filter
	assert_ulong_equal(lzma1_filters[0].id, lzma1_test_filters[0].id);
	assert_ulong_equal(NULL, lzma1_test_filters[0].options);
	// Test Delta filter
	assert_ulong_equal(lzma1_filters[1].id, lzma1_test_filters[1].id);
	lzma_options_delta *test_delta_lzma1 = lzma1_test_filters[1].options;
	lzma_options_delta *expected_delta_lzma1 = lzma1_filters[1].options;
	assert_n_array_equal(((uint8_t*) expected_delta_lzma1),
			((uint8_t*) test_delta_lzma1),
			sizeof(lzma_options_delta));
	// Test LZMA1 filter
	assert_ulong_equal(lzma1_filters[2].id, lzma1_test_filters[2].id);
	compare_lzma_filters(lzma1_filters[2].options,
			lzma1_test_filters[2].options);
	// Test terminator filter
	assert_ulong_equal(LZMA_VLI_UNKNOWN, lzma1_test_filters[3].id);
#endif
#ifdef TEST_FILTER_CHAIN_LZMA2
	lzma_filter lzma2_test_filters[LZMA_FILTERS_MAX + 1];
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			expected_lzma2_filters_str));
	// Test BCJ filter
	assert_ulong_equal(lzma2_filters[0].id, lzma2_test_filters[0].id);
	lzma_options_bcj *test_bcj_lzma2 = lzma2_test_filters[0].options;
	lzma_options_bcj *expected_bcj_lzma2 = lzma2_filters[0].options;
	assert_n_array_equal(((uint8_t*) expected_bcj_lzma2),
			((uint8_t*) test_bcj_lzma2),
			sizeof(lzma_options_bcj));
	// Test Delta filter
	assert_ulong_equal(lzma2_filters[1].id, lzma2_test_filters[1].id);
	lzma_options_delta *test_delta_lzma2 = lzma2_test_filters[1].options;
	lzma_options_delta *expected_delta_lzma2 = lzma2_filters[1].options;
	assert_n_array_equal(((uint8_t*) expected_delta_lzma2),
			((uint8_t*) test_delta_lzma2),
			sizeof(lzma_options_delta));
	// Test LZMA2 filter
	assert_ulong_equal(lzma2_filters[2].id, lzma2_test_filters[2].id);
	compare_lzma_filters(lzma2_filters[2].options,
			lzma2_test_filters[2].options);
	// Test terminator filter
	assert_ulong_equal(LZMA_VLI_UNKNOWN, lzma2_test_filters[3].id);

	// Test specifying all possible presets
	for (int i = 0; i < 10; i++)
		test_lzma2_preset_match(i);

	// Test using "fast" mode by specifying it in a string
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=mode:fast"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	lzma_options_lzma *options = lzma2_test_filters[0].options;
	assert_int_equal(LZMA_MODE_FAST, options->mode);

	// Test using "normal" mode by specifying it in a string
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=mode:normal"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	options = lzma2_test_filters[0].options;
	assert_int_equal(LZMA_MODE_NORMAL, options->mode);

	// Test setting dict_size value with k, kiB, M, and MiB
	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=dict_size:4096k"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	options = lzma2_test_filters[0].options;
	assert_int_equal(4194304, options->dict_size);

	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=dict_size:4096kiB"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	options = lzma2_test_filters[0].options;
	assert_int_equal(4194304, options->dict_size);

	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=dict_size:40M"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	options = lzma2_test_filters[0].options;
	assert_int_equal(41943040, options->dict_size);

	assert_int_equal(LZMA_OK, lzma_str_to_filters(
			lzma2_test_filters, NULL,
			"lzma2=dict_size:40MiB"));
	assert_ulong_equal(LZMA_FILTER_LZMA2, lzma2_test_filters[0].id);
	options = lzma2_test_filters[0].options;
	assert_int_equal(41943040, options->dict_size);
#endif
}


static void
test_str_to_filter_expect_fail(void)
{
	lzma_filter filters[LZMA_FILTERS_MAX + 1];

#ifdef TEST_FILTER_CHAIN_LZMA2
	// Test NULL args
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, NULL));
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(NULL,
			NULL, "lzma2"));
	// Test empty string
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, ""));
	// Test just the delimiter character in a string
	char delim_only_str[] = {LZMA_FILTER_DELIMITER, 0};
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, delim_only_str));
	// Test filter name to option mismatches
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, "delta=start_offset:12"));
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, "x86=dist:12"));
	// Test using an invalid preset value
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, "lzma2=12"));
	// Test when two delimiters are back to back
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, "delta++lzma2"));
	// Test when two option separators are back to back
	assert_int_equal(LZMA_PROG_ERROR, lzma_str_to_filters(filters,
			NULL, "delta+lzma2=lp:2,,pb:0"));
#endif
}


void
test_lzma_filter_str_conversion(void)
{
	test_fixture_start();
	run_test(test_filter_to_str_expect_pass);
	run_test(test_filter_to_str_expect_fail);
	run_test(test_str_to_filter_expect_pass);
	run_test(test_str_to_filter_expect_fail);
	test_fixture_end();
}
