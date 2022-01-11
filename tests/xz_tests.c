///////////////////////////////////////////////////////////////////////////////
//
/// \file       xz_tests.c
/// \brief      Combines all tests for xz util binaries
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "xz_tests.h"

static void
all_tests(void)
{
	test_xz_decompress();
	test_xz_compress();
}

int
main(int argc, char **argv)
{
	return stest_testrunner(argc, argv, all_tests, NULL, NULL);
}
