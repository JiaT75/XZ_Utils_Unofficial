///////////////////////////////////////////////////////////////////////////////
//
/// \file       liblzma_tests.c
/// \brief      Combines all tests for liblzma
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"


static void
all_tests(void)
{
	test_integrity_checks();
	test_block_header_coders();
	test_bcj_filter();
	test_filter_flags();
	test_lzma_index_structure();
	test_stream_header_and_footer_coders();
	test_lzma_filters();
	test_lzma_raw();
	test_lzma_properties();
	test_lzma_filter_str_conversion();
}

int
main(int argc, char **argv)
{
	return stest_testrunner(argc, argv, all_tests, NULL, NULL);
}
