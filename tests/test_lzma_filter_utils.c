///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_lzma_filter_utils.c
/// \brief      Implements global filters useful for tesing functions exported
///             from src/liblzma/api/lzma/filter.h
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////


#include "test_lzma_filter_utils.h"
#include "config.h"
#include "lzma.h"

// Creating sample lzma1 and lzma2 filter chains as globals since
// they are needed for multiple tests

#if TEST_FILTER_CHAIN_LZMA1
lzma_options_bcj bcj_ops_lzma1 = {
	.start_offset = 0
};

lzma_options_delta delta_ops_lzma1 = {
	.type = LZMA_DELTA_TYPE_BYTE,
	.dist = LZMA_DELTA_DIST_MIN
};

lzma_options_lzma lzma1_ops = {
	.dict_size = LZMA_DICT_SIZE_DEFAULT,
	.preset_dict = NULL,
	.preset_dict_size = 0,
	.lc = LZMA_LCLP_MIN,
	.lp = LZMA_LP_DEFAULT,
	.pb = LZMA_PB_MIN,
	.mode = LZMA_MODE_FAST,
	.nice_len = 32,
	.mf = LZMA_MF_HC3,
	.depth = 0
};

// Filter chain x86 -> delta -> LZMA1 -> null terminator
lzma_filter lzma1_filters[4] = {
	{
		.id = LZMA_FILTER_X86,
		.options = &bcj_ops_lzma1
	},
	{
		.id = LZMA_FILTER_DELTA,
		.options = &delta_ops_lzma1
	},
	{
		.id = LZMA_FILTER_LZMA1,
		.options = &lzma1_ops
	},
	{
		.id = LZMA_VLI_UNKNOWN,
		.options = NULL
	}
};
#endif

#if TEST_FILTER_CHAIN_LZMA2
lzma_options_bcj bcj_ops_lzma2 = {
	.start_offset = 16
};

lzma_options_delta delta_ops_lzma2 = {
	.type = LZMA_DELTA_TYPE_BYTE,
	.dist = LZMA_DELTA_DIST_MAX
};

lzma_options_lzma lzma2_ops= {
	.dict_size = LZMA_DICT_SIZE_DEFAULT,
	.preset_dict = NULL,
	.preset_dict_size = 0,
	.lc = LZMA_LCLP_MAX,
	.lp = LZMA_LP_DEFAULT,
	.pb = LZMA_PB_MAX,
	.mode = LZMA_MODE_NORMAL,
	.nice_len = 273,
	.mf = LZMA_MF_HC4,
	.depth = 200
};

// Filter chain arm -> delta -> LZMA2 -> null terminator
lzma_filter lzma2_filters[4] = {
	{
		.id = LZMA_FILTER_ARM,
		.options = &bcj_ops_lzma2
	},
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
#endif

#if TEST_FILTER_CHAIN_INVALID
// These filters together are invalid because lzma1 cannot be
// the first filter and delta cannot be the final filter
// Useful for testing error cases
lzma_filter invalid_filters[2] = {
	{
		.id = LZMA_FILTER_LZMA2,
		.options = &lzma1_ops
	},
	{
		.id = LZMA_FILTER_DELTA,
		.options = &delta_ops_lzma1
	}
};
#endif

// Creating one filter of each type without any options
// Useful in testing filter flags and properties size functions
// Checking if the encoders are used because the filter flags and
// property size functions use the internal encoder structures
lzma_filter basic_filters[] = {
#ifdef HAVE_ENCODER_LZMA1
	{
		.id = LZMA_FILTER_LZMA1,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_LZMA2
	{
		.id = LZMA_FILTER_LZMA2,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_X86
	{
		.id = LZMA_FILTER_X86,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_POWERPC
	{
		.id = LZMA_FILTER_POWERPC,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_IA64
	{
		.id = LZMA_FILTER_IA64,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_ARM
	{
		.id = LZMA_FILTER_ARM,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	{
		.id = LZMA_FILTER_ARMTHUMB,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_SPARC
	{
		.id = LZMA_FILTER_SPARC,
		.options = NULL
	},
#endif
#ifdef HAVE_ENCODER_DELTA
	{
		.id = LZMA_FILTER_DELTA,
		.options = NULL
	},
#endif
	};
