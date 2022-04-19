///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_lzma_filter_utils.h
/// \brief      Declares global filters useful for tesing functions exported
///             from src/liblzma/api/lzma/filter.h
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////


#ifndef TEST_LZMA_FILTER_UTILS_H
#define TEST_LZMA_FILTER_UTILS_H

#include "config.h"
#include "lzma.h"

// Used when looping over possible filter ids
// searching for an unexpected "supported" filter ID
#define LZMA_FILTER_MAX_ID_CHECK 1000

// Since defined() is not portable, using chains of #ifdefs
// to represent logical AND
// In order to use the predefined any of the predefiend filter chain
// defined in test_lzma_filter_utils.c, the LZMA1 filter, DELTA filter, and
// X86 filters are all required
// TEST_FILTER_CHAIN_XXX is defined if the filter can either be
// encoded OR decoded, so we must be careful to only define it once
#ifdef HAVE_ENCODER_LZMA1
#ifdef HAVE_ENCODER_DELTA
#ifdef HAVE_ENCODER_X86
#define TEST_FILTER_CHAIN_ENCODER_LZMA1 1
#define TEST_FILTER_CHAIN_LZMA1 1
#endif
#endif
#endif

#ifdef HAVE_DECODER_LZMA1
#ifdef HAVE_DECODER_DELTA
#ifdef HAVE_DECODER_X86
#define TEST_FILTER_CHAIN_DECODER_LZMA1 1
#ifndef TEST_FILTER_CHAIN_LZMA1
#define TEST_FILTER_CHAIN_LZMA1 1
#endif
#endif
#endif
#endif

#ifdef HAVE_ENCODER_LZMA2
#ifdef HAVE_ENCODER_DELTA
#ifdef HAVE_ENCODER_ARM
#define TEST_FILTER_CHAIN_ENCODER_LZMA2 1
#define TEST_FILTER_CHAIN_LZMA2 1
#endif
#endif
#endif

#ifdef HAVE_DECODER_LZMA2
#ifdef HAVE_DECODER_DELTA
#ifdef HAVE_DECODER_ARM
#define TEST_FILTER_CHAIN_DECODER_LZMA2 1
#ifndef TEST_FILTER_CHAIN_LZMA2
#define TEST_FILTER_CHAIN_LZMA2 1
#endif
#endif
#endif
#endif

#ifdef HAVE_ENCODER_LZMA2
#ifdef HAVE_ENCODER_DELTA
#define TEST_FILTER_CHAIN_ENCODER_INVALID 1
#define TEST_FILTER_CHAIN_INVALID 1
#endif
#endif

#ifdef HAVE_DECODER_LZMA2
#ifdef HAVE_DECODER_DELTA
#define TEST_FILTER_CHAIN_DECODER_INVALID 1
#ifndef TEST_FILTER_CHAIN_INVALID
#define TEST_FILTER_CHAIN_INVALID 1
#endif
#endif
#endif

// Creating sample lzma1 and lzma2 filter chains as globals since
// they are needed for multiple tests

extern lzma_options_bcj bcj_ops_lzma1;

extern lzma_options_delta delta_ops_lzma1;

extern lzma_options_lzma lzma1_ops;

// Filter chain x86 -> delta -> LZMA1 -> null terminator
extern lzma_filter lzma1_filters[4];

extern lzma_options_bcj bcj_ops_lzma2;

extern lzma_options_delta delta_ops_lzma2;

extern lzma_options_lzma lzma2_ops;

// Filter chain arm -> delta -> LZMA2 -> null terminator
extern lzma_filter lzma2_filters[4];

// Creating an invalid filter chain to test correct error codes
extern lzma_filter invalid_filters[2];

// Creating one filter of each type without any options
// Useful in testing filter flags and properties size functions
// Using the #ifdefs to correctly calculate the size of this array
// The size is essential in order to determine the length using sizeof
// in the other test files
extern lzma_filter basic_filters[
#ifdef HAVE_ENCODER_LZMA1
	1 +
#endif
#ifdef HAVE_ENCODER_LZMA2
	1 +
#endif
#ifdef HAVE_ENCODER_X86
	1 +
#endif
#ifdef HAVE_ENCODER_POWERPC
	1 +
#endif
#ifdef HAVE_ENCODER_IA64
	1 +
#endif
#ifdef HAVE_ENCODER_ARM
	1 +
#endif
#ifdef HAVE_ENCODER_ARMTHUMB
	1 +
#endif
#ifdef HAVE_ENCODER_SPARC
	1 +
#endif
#ifdef HAVE_ENCODER_DELTA
	1 +
#endif
	0
];

#endif
