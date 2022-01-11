///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_xz_compress.c
/// \brief      Tests xz compression with various options
///
/// \todo       Add subblock tests when stable
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////


#include "xz_tests.h"
#include "test_utils.h"
#include "sysdefs.h"

static bool file_exists(const char *filename);
static FILE *file_create(const char *filename);
static void file_finish(FILE *file, const char *filename);
static void write_random(FILE *file);

// Tests are unrolled instead of in a loop to improve readability
// on a failing test and show explicitly what is being tested
static void test_xz_compress_and_decompress(const char* option);
static void test_xz_compress_level_1(void);
static void test_xz_compress_level_2(void);
static void test_xz_compress_level_3(void);
static void test_xz_compress_level_4(void);
static void test_xz_compress_delta_dist_1(void);
static void test_xz_compress_delta_dist_4(void);
static void test_xz_compress_delta_dist_256(void);
static void test_xz_compress_x86(void);
static void test_xz_compress_powerpc(void);
static void test_xz_compress_ia64(void);
static void test_xz_compress_arm(void);
static void test_xz_compress_armthumb(void);
static void test_xz_compress_sparc(void);

static const char* xz_compressed_tmp_filename = "tmp_compressed";
static const char* xz_decompressed_tmp_filename = "tmp_uncompressed";
static const char* xz_options = "--memlimit-compress=48MiB --memlimit-decompress=5MiB " \
		                        "--no-adjust --threads=1 --check=crc64";
static const char* compress_filenames[] = {"compress_generated_abc",
                                         "compress_generated_random",
                                         "compress_generated_text",
                                         "compress_prepared_bcj_sparc",
                                         "compress_prepared_bcj_x86"};
#define COMPRESS_FILE_COUNT 5


// Avoid re-creating the test files every time the tests are run.
#define create_test(name) \
do { \
	if (!file_exists("compress_generated_" #name)) { \
		FILE *file = file_create("compress_generated_" #name); \
		write_ ## name(file); \
		file_finish(file, "compress_generated_" #name); \
	} \
} while (0)

static bool
file_exists(const char *filename)
{
	// Trying to be somewhat portable by avoiding stat().
	FILE *file = fopen(filename, "rb");
	bool ret;

	if (file != NULL) {
		fclose(file);
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}


static FILE *
file_create(const char *filename)
{
	FILE *file = fopen(filename, "wb");

	if (file == NULL) {
		perror(filename);
	}

	return file;
}


static void
file_finish(FILE *file, const char *filename)
{
	const bool ferror_fail = ferror(file);
	const bool fclose_fail = fclose(file);

	if (ferror_fail || fclose_fail) {
		perror(filename);
	}
}


// File that repeats "abc\n" a few thousand times. This is targeted
// especially at Subblock filter's run-length encoder.
static void
write_abc(FILE *file)
{
	for (size_t i = 0; i < 12345; ++i)
		if (fwrite("abc\n", 4, 1, file) != 1)
			printf("Error writing to file\n");
}


// File that doesn't compress. We always use the same random seed to
// generate identical files on all systems.
static void
write_random(FILE *file)
{
	uint32_t n = 5;

	for (size_t i = 0; i < 123456; ++i) {
		n = 101771 * n + 71777;

		putc((uint8_t)(n), file);
		putc((uint8_t)(n >> 8), file);
		putc((uint8_t)(n >> 16), file);
		putc((uint8_t)(n >> 24), file);
	}
}


// Text file
static void
write_text(FILE *file)
{
	static const char *lorem[] = {
		"Lorem", "ipsum", "dolor", "sit", "amet,", "consectetur",
		"adipisicing", "elit,", "sed", "do", "eiusmod", "tempor",
		"incididunt", "ut", "labore", "et", "dolore", "magna",
		"aliqua.", "Ut", "enim", "ad", "minim", "veniam,", "quis",
		"nostrud", "exercitation", "ullamco", "laboris", "nisi",
		"ut", "aliquip", "ex", "ea", "commodo", "consequat.",
		"Duis", "aute", "irure", "dolor", "in", "reprehenderit",
		"in", "voluptate", "velit", "esse", "cillum", "dolore",
		"eu", "fugiat", "nulla", "pariatur.", "Excepteur", "sint",
		"occaecat", "cupidatat", "non", "proident,", "sunt", "in",
		"culpa", "qui", "officia", "deserunt", "mollit", "anim",
		"id", "est", "laborum."
	};

	// Let the first paragraph be the original text.
	for (size_t w = 0; w < ARRAY_SIZE(lorem); ++w) {
		fprintf(file, "%s ", lorem[w]);

		if (w % 7 == 6)
			fprintf(file, "\n");
	}

	// The rest shall be (hopefully) meaningless combinations of
	// the same words.
	uint32_t n = 29;

	for (size_t p = 0; p < 500; ++p) {
		fprintf(file, "\n\n");

		for (size_t w = 0; w < ARRAY_SIZE(lorem); ++w) {
			n = 101771 * n + 71777;

			fprintf(file, "%s ", lorem[n % ARRAY_SIZE(lorem)]);

			if (w % 7 == 6)
				fprintf(file, "\n");
		}
	}
}

static void
test_xz_compress_and_decompress(const char* option)
{
    for(int i = 0; i < COMPRESS_FILE_COUNT; i++){
		const char* current_file = compress_filenames[i];
		assert_int_equal(systemf("%s %s -c %s %s > %s",
						XZ_ABS_PATH, xz_options, option, current_file,
						xz_compressed_tmp_filename), 0);

		assert_int_equal(systemf("%s %s -cd > %s", XZ_ABS_PATH, xz_compressed_tmp_filename,
						xz_decompressed_tmp_filename), 0);
		assert_int_equal(systemf("cmp %s %s", xz_decompressed_tmp_filename, current_file), 0);
    }
}

/* TODO - Add subblock tests when stable
* --subblock
* --subblock=size=1
* --subblock=size=1,rle=1
* --subblock=size=1,rle=4
* --subblock=size=4,rle=4
* --subblock=size=8,rle=4
* --subblock=size=8,rle=8
* --subblock=size=4096,rle=12
*/

static void
test_xz_compress_level_1(void)
{
	test_xz_compress_and_decompress("-1");
}

static void
test_xz_compress_level_2(void)
{
	test_xz_compress_and_decompress("-2");
}

static void
test_xz_compress_level_3(void)
{
	test_xz_compress_and_decompress("-3");
}

static void
test_xz_compress_level_4(void)
{
	test_xz_compress_and_decompress("-4");
}

static void
test_xz_compress_delta_dist_1(void)
{
	test_xz_compress_and_decompress("--delta=dist=1 --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_delta_dist_4(void)
{
	test_xz_compress_and_decompress("--delta=dist=4 --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_delta_dist_256(void)
{
	test_xz_compress_and_decompress("--delta=dist=256 --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_x86(void)
{
	test_xz_compress_and_decompress("--x86 --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_powerpc(void)
{
	test_xz_compress_and_decompress("--powerpc --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_ia64(void)
{
	test_xz_compress_and_decompress("--ia64 --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_arm(void)
{
	test_xz_compress_and_decompress("--arm --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_armthumb(void)
{
	test_xz_compress_and_decompress("--armthumb --lzma2=dict=64KiB,nice=32,mode=fast");
}

static void
test_xz_compress_sparc(void)
{
	test_xz_compress_and_decompress("--sparc --lzma2=dict=64KiB,nice=32,mode=fast");
}

void
test_xz_compress(void)
{
	create_test(abc);
	create_test(random);
	create_test(text);

	test_fixture_start();
	if(!can_xz() || !can_xz_dec()){
		printf("xz or xzdec not built. Skipping xz compression tests\n");
	}
	else {
		run_test(test_xz_compress_level_1);
		run_test(test_xz_compress_level_2);
		run_test(test_xz_compress_level_3);
		run_test(test_xz_compress_level_4);
		run_test(test_xz_compress_delta_dist_1);
		run_test(test_xz_compress_delta_dist_4);
		run_test(test_xz_compress_delta_dist_256);
		run_test(test_xz_compress_x86);
		run_test(test_xz_compress_powerpc);
		run_test(test_xz_compress_ia64);
		run_test(test_xz_compress_arm);
		run_test(test_xz_compress_armthumb);
		run_test(test_xz_compress_sparc);
	}
	test_fixture_end();
}
