///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_xz_decompress.c
/// \brief      Tests decompression with various options
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "xz_tests.h"
#include "test_utils.h"

static void test_good_files_xz(void);
static void test_good_files_xz_dec(void);
static void test_bad_files_xz(void);
static void test_bad_files_xz_dec(void);

static void good_files_xz_cb(char* path);
static void good_files_xz_dec_cb(char* path);
static void bad_files_xz_cb(char* path);
static void bad_files_xz_dec_cb(char* path);

static void
good_files_xz_cb(char* path)
{
	assert_true(systemf("%s -dc %s > /dev/null 2>&1", XZ_ABS_PATH, path) == 0);
}

static void
test_good_files_xz(void)
{
	glob_and_callback("files/good-*.xz", (glob_callback) good_files_xz_cb);
}

static void
good_files_xz_dec_cb(char* path)
{
	assert_true(systemf("%s %s > /dev/null 2>&1", XZ_DEC_ABS_PATH, path) == 0);
}

static void
test_good_files_xz_dec(void)
{
	glob_and_callback("files/good-*.xz", (glob_callback) good_files_xz_dec_cb);
}

static void
bad_files_xz_cb(char* path)
{
	assert_false(systemf("%s -dc %s > /dev/null 2>&1", XZ_ABS_PATH, path) == 0);
}

static void
test_bad_files_xz(void)
{
	glob_and_callback("files/bad-*.xz", (glob_callback) bad_files_xz_cb);
}

static void
bad_files_xz_dec_cb(char* path)
{
	assert_false(systemf("%s %s > /dev/null 2>&1", XZ_DEC_ABS_PATH, path) == 0);
}

static void
test_bad_files_xz_dec(void)
{
	glob_and_callback("files/bad-*.xz", (glob_callback) bad_files_xz_dec_cb);
}

void
test_xz_decompress(void)
{
	test_fixture_start();
	if(!can_glob()){
		printf("Globbing is not supported on this platform. Skipping tests\n");
	}
	else {
		if(!can_xz()){
			printf("xz not built. Skipping xz tests\n");
		}
		else {
			run_test(test_good_files_xz);
			run_test(test_bad_files_xz);
		}

		if(!can_xz_dec()){
			printf("xzdec not built. Skipping xz_dec tests\n");
		}
		else {
			run_test(test_good_files_xz_dec);
			run_test(test_bad_files_xz_dec);
		}
	}

	test_fixture_end();
}
