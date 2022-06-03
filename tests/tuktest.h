///////////////////////////////////////////////////////////////////////////////
//
/// \file       tuktest.h
/// \brief      Helper macros for writing simple test programs
/// \version    2022-06-02
///
/// Some inspiration was taken from STest by Keith Nicholas.
///
/// This is standard C99/C11 only and thus should be fairly portable
/// outside POSIX systems too.
///
/// This supports putting multiple tests in a single test program
/// although it is perfectly fine to have only one test per program.
/// Each test can produce one of these results:
///   - Pass
///   - Fail
///   - Skip
///   - Hard error (the remaining tests, if any, are not run)
///
/// By default this produces an exit status that is compatible with
/// Automake and Meson, and mostly compatible with CMake.[1]
/// If a test program contains multiple tests, only one exit code can
/// be returned. Of the following, the first match is used:
///   - 99 if any test returned a hard error
///   - stdlib.h's EXIT_FAILURE if at least one test failed
///   - 77 if at least one test was skipped or no tests were run at all
///   - stdlib.h's EXIT_SUCCESS (0 on POSIX); that is, if none of the above
///     are true then there was at least one test to run and none of them
///     failed, was skipped, or returned a hard error.
///
/// A summary of tests being run and their results are printed to stdout.
/// If you want ANSI coloring for the output, #define TUKTEST_COLOR.
/// If you only want output when something goes wrong, #define TUKTEST_QUIET.
///
/// The downside of the above mapping is that it cannot indicate if
/// some tests were skipped and some passed. If that is likely to
/// happen it may be better to split into multiple test programs (one
/// test per program) or use the TAP mode described below.
///
/// By using #define TUKTEST_TAP before #including this file the
/// output will be Test Anything Protocol (TAP) version 12 compatible
/// and the exit status will always be EXIT_SUCCESS. This can be easily
/// used with Automake via its tap-driver.sh. Meson supports TAP natively.
/// TAP's todo-directive isn't supported for now, mostly because it's not
/// trivially convertible to the exit-status reporting method.
///
/// If TUKTEST_TAP is used, TUKTEST_QUIET and TUKTEST_COLOR are ignored.
///
/// The main() function may look like this (remember to include config.h
/// or such files too if needed!):
///
///     #include "tuktest.h"
///
///     int main(int argc, char **argv)
///     {
///         tuktest_start(argc, argv);
///
///         if (!is_package_foo_available())
///             tuktest_early_skip("Optional package foo is not available");
///
///         if (!do_common_initializations())
///             tuktest_error("Error during common initializations");
///
///         tuktest_run(testfunc1);
///         tuktest_run(testfunc2);
///
///         return tuktest_end();
///     }
///
/// Using exit(tuktest_end()) as a pair to tuktest_start() is OK too.
///
/// Each test function called via tuktest_run() should be of type
/// "void testfunc1(void)". The test functions should use the
/// various assert_CONDITION() macros. The current test stops if
/// an assertion fails (this is implemented with setjmp/longjmp).
/// Execution continues from the next test unless the failure was
/// due to assert_error() (indicating a hard error) which makes
/// the program exit() without running any remaining tests.
///
/// Search for "define assert" in this file to find the explanations
/// of the available assertion macros.
///
/// IMPORTANT:
///
///   - The assert_CONDITION() macros may only be used by code that is
///     called via tuktest_run()! This includes not only the function
///     named in the tuktest_run() call but also any functions called
///     further from there. (The assert_CONDITION() macros depend on setup
///     code in tuktest_run() and other use results in undefined behavior.)
///
///   - The limitations goes the other way too: Functions and macros
///     other than the assert_CONDITION() macros must not be used in
///     the tests called via tuktest_run().
///
/// Footnotes:
///
/// [1] As of 2022-06-02:
///     See the Automake manual "info (automake)Scripts-based Testsuites" or:
///     https://www.gnu.org/software/automake/manual/automake.html#Scripts_002dbased-Testsuites
///
///     Meson: https://mesonbuild.com/Unit-tests.html
///
///     CMake handles passing and failing tests by default but treats hard
///     errors as regular fails. To CMake support skipped tests correctly,
///     one has to set the SKIP_RETURN_CODE property for each test:
///
///     set_tests_properties(foo_test_name PROPERTIES SKIP_RETURN_CODE 77)
///
///     See:
///     https://cmake.org/cmake/help/latest/command/set_tests_properties.html
///     https://cmake.org/cmake/help/latest/prop_test/SKIP_RETURN_CODE.html
//
//  Author:    Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TUKTEST_H
#define TUKTEST_H

#include <stddef.h>

// On some (too) old systems inttypes.h doesn't exist or isn't good enough.
// Include it conditionally so that any portability tricks can be done before
// tuktest.h is included. On any modern system inttypes.h is fine as is.
#ifndef PRIu64
#	include <inttypes.h>
#endif

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#	define TUKTEST_GNUC_REQ(major, minor) \
		((__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)) \
			|| __GNUC__ > (major))
#else
#	define TUKTEST_GNUC_REQ(major, minor) 0
#endif


// We need printf("") so silence the warning about empty format string.
#if TUKTEST_GNUC_REQ(4, 2)
#	pragma GCC diagnostic ignored "-Wformat-zero-length"
#endif


// Types and printf format macros to use in integer assertions and also for
// printing size_t values (C99's %zu isn't available on very old systems).
typedef int64_t tuktest_int;
typedef uint64_t tuktest_uint;
#define TUKTEST_PRId PRId64
#define TUKTEST_PRIu PRIu64
#define TUKTEST_PRIX PRIX64


// When TAP mode isn't used, Automake-compatible exit statuses are used.
#define TUKTEST_EXIT_PASS EXIT_SUCCESS
#define TUKTEST_EXIT_FAIL EXIT_FAILURE
#define TUKTEST_EXIT_SKIP 77
#define TUKTEST_EXIT_ERROR 99


enum tuktest_result {
	TUKTEST_PASS,
	TUKTEST_FAIL,
	TUKTEST_SKIP,
	TUKTEST_ERROR,
};


#ifdef TUKTEST_TAP
#	undef TUKTEST_QUIET
#	undef TUKTEST_COLOR
#	undef TUKTEST_TAP
#	define TUKTEST_TAP 1
#	define TUKTEST_STR_PASS  "ok -"
#	define TUKTEST_STR_FAIL  "not ok -"
#	define TUKTEST_STR_SKIP  "ok - # SKIP"
#	define TUKTEST_STR_ERROR "Bail out!"
#else
#	define TUKTEST_TAP 0
#	ifdef TUKTEST_COLOR
#		define TUKTEST_COLOR_PASS  "\x1B[0;32m"
#		define TUKTEST_COLOR_FAIL  "\x1B[0;31m"
#		define TUKTEST_COLOR_SKIP  "\x1B[1;34m"
#		define TUKTEST_COLOR_ERROR "\x1B[0;35m"
#		define TUKTEST_COLOR_TOTAL "\x1B[1m"
#		define TUKTEST_COLOR_OFF   "\x1B[m"
#		define TUKTEST_COLOR_IF(cond, color) ((cond) ? (color) : "" )
#	else
#		define TUKTEST_COLOR_PASS  ""
#		define TUKTEST_COLOR_FAIL  ""
#		define TUKTEST_COLOR_SKIP  ""
#		define TUKTEST_COLOR_ERROR ""
#		define TUKTEST_COLOR_TOTAL ""
#		define TUKTEST_COLOR_OFF   ""
#		define TUKTEST_COLOR_IF(cond, color) ""
#	endif
#	define TUKTEST_COLOR_ADD(str, color) color str TUKTEST_COLOR_OFF
#	define TUKTEST_STR_PASS \
		TUKTEST_COLOR_ADD("PASS:", TUKTEST_COLOR_PASS)
#	define TUKTEST_STR_FAIL \
		TUKTEST_COLOR_ADD("FAIL:", TUKTEST_COLOR_FAIL)
#	define TUKTEST_STR_SKIP \
		TUKTEST_COLOR_ADD("SKIP:", TUKTEST_COLOR_SKIP)
#	define TUKTEST_STR_ERROR \
		TUKTEST_COLOR_ADD("ERROR:", TUKTEST_COLOR_ERROR)
#endif

// NOTE: If TUKTEST_TAP is defined then TUKTEST_QUIET will get undefined above.
#ifndef TUKTEST_QUIET
#	define TUKTEST_QUIET 0
#else
#	undef TUKTEST_QUIET
#	define TUKTEST_QUIET 1
#endif


// Counts of the passed, failed, skipped, and hard-errored tests.
// This is indexed with the enumeration constants from enum tuktest_result.
static unsigned tuktest_stats[4] = { 0, 0, 0, 0 };

// Copy of argc and argv from main(). These are set by tuktest_start().
static int tuktest_argc = 0;
static char **tuktest_argv = NULL;

// Name of the currently-running test. This exists because it's nice
// to print the main test function name even if the failing test-assertion
// fails in a function called by the main test function.
static const char *tuktest_name = NULL;

// longjmp() target for when a test-assertion fails.
static jmp_buf tuktest_jmpenv;


// printf() is without checking its return value in many places. This function
// is called before exiting to check the status of stdout and catch errors.
static void
tuktest_catch_stdout_errors(void)
{
	if (ferror(stdout) || fclose(stdout)) {
		fputs("Error while writing to stdout\n", stderr);
		exit(TUKTEST_EXIT_ERROR);
	}
}


// A simplified basename()-like function that is good enough for
// cleaning up __FILE__. This supports / and \ as path separator.
// If the path separator is wrong then the full path will be printed;
// it's a cosmetic problem only.
static const char *
tuktest_basename(const char *filename)
{
	for (const char *p = filename + strlen(filename); p > filename; --p)
		if (*p == '/' || *p == '\\')
			return p + 1;

	return filename;
}


/// Initialize the test framework. No other functions or macros
/// from this file may be called before calling this.
///
/// If the arguments from main() aren't available, use 0 and NULL.
/// If these are set, then only a subset of tests can be run by
/// specifying their names on the command line.
#define tuktest_start(argc, argv) \
do { \
	tuktest_argc = argc; \
	tuktest_argv = argv; \
	if (!TUKTEST_TAP && !TUKTEST_QUIET) \
		printf("=== %s ===\n", tuktest_basename(__FILE__)); \
} while (0)


/// If it can be detected early that no tests can be run, this macro can
/// be called after tuktest_start() but before any tuktest_run() to print
/// a reason why the tests were skipped. Note that this macro calls exit().
///
/// Using "return tuktest_end();" in main() when no tests were run has
/// the same result as tuktest_early_skip() except that then no reason
/// for the skipping can be printed.
#define tuktest_early_skip(...) \
do { \
	printf("%s [%s:%u] ", \
			TUKTEST_TAP ? "1..0 # SKIP" : TUKTEST_STR_SKIP, \
			tuktest_basename(__FILE__), __LINE__); \
	printf(__VA_ARGS__); \
	printf("\n"); \
	if (!TUKTEST_TAP && !TUKTEST_QUIET) \
		printf("=== END ===\n"); \
	tuktest_catch_stdout_errors(); \
	exit(TUKTEST_TAP ? EXIT_SUCCESS : TUKTEST_EXIT_SKIP); \
} while (0)


/// Some test programs need to do initializations before or between
/// calls to tuktest_run(). If such initializations unexpectedly fail,
/// tuktest_error() can be used to report it as a hard error outside
/// test functions, for example, in main(). Then the remaining tests
/// won't be run (this macro calls exit()).
///
/// Typically tuktest_error() would be used before any tuktest_run()
/// calls but it is also possible to use tuktest_error() after one or
/// more tests have been run with tuktest_run(). This is in contrast to
/// tuktest_early_skip() which must never be called after tuktest_run().
///
/// NOTE: tuktest_start() must have been called before tuktest_error().
///
/// NOTE: This macro MUST NOT be called from test functions running under
/// tuktest_run()! Use assert_error() to report a hard error in code that
/// is running under tuktest_run().
#define tuktest_error(...) \
do { \
	++tuktest_stats[TUKTEST_ERROR]; \
	printf(TUKTEST_STR_ERROR " [%s:%u] ", \
			tuktest_basename(__FILE__), __LINE__); \
	printf(__VA_ARGS__); \
	printf("\n"); \
	exit(tuktest_end()); \
} while (0)


/// At the end of main() one should have "return tuktest_end();" which
/// prints the stats or the TAP plan, and handles the exit status.
/// Using exit(tuktest_end()) is OK too.
///
/// If the test program can detect early that all tests must be skipped,
/// then tuktest_early_skip() may be useful so that the reason why the
/// tests were skipped can be printed.
static int
tuktest_end(void)
{
	unsigned total_tests = 0;
	for (unsigned i = 0; i <= TUKTEST_ERROR; ++i)
		total_tests += tuktest_stats[i];

	if (tuktest_stats[TUKTEST_ERROR] == 0 && tuktest_argc > 1
			&& (unsigned)(tuktest_argc - 1) > total_tests) {
		printf(TUKTEST_STR_ERROR " Fewer tests were run than "
				"specified on the command line. "
				"Was a test name mistyped?\n");
		++tuktest_stats[TUKTEST_ERROR];
	}

#if TUKTEST_TAP
	// Print the plan only if no "Bail out!" has occurred.
	// Print the skip directive if no tests were run.
	// We cannot know the reason for the skip here though
	// (see tuktest_early_skip()).
	if (tuktest_stats[TUKTEST_ERROR] == 0)
		printf("1..%u%s\n", total_tests,
				total_tests == 0 ? " # SKIP" : "");

	tuktest_catch_stdout_errors();
	return EXIT_SUCCESS;
#else
	if (!TUKTEST_QUIET)
		printf("---\n"
				"%s# TOTAL: %u" TUKTEST_COLOR_OFF "\n"
				"%s# PASS:  %u" TUKTEST_COLOR_OFF "\n"
				"%s# SKIP:  %u" TUKTEST_COLOR_OFF "\n"
				"%s# FAIL:  %u" TUKTEST_COLOR_OFF "\n"
				"%s# ERROR: %u" TUKTEST_COLOR_OFF "\n"
				"=== END ===\n",
				TUKTEST_COLOR_TOTAL,
				total_tests,
				TUKTEST_COLOR_IF(
					tuktest_stats[TUKTEST_PASS] > 0,
					TUKTEST_COLOR_PASS),
				tuktest_stats[TUKTEST_PASS],
				TUKTEST_COLOR_IF(
					tuktest_stats[TUKTEST_SKIP] > 0,
					TUKTEST_COLOR_SKIP),
				tuktest_stats[TUKTEST_SKIP],
				TUKTEST_COLOR_IF(
					tuktest_stats[TUKTEST_FAIL] > 0,
					TUKTEST_COLOR_FAIL),
				tuktest_stats[TUKTEST_FAIL],
				TUKTEST_COLOR_IF(
					tuktest_stats[TUKTEST_ERROR] > 0,
					TUKTEST_COLOR_ERROR),
				tuktest_stats[TUKTEST_ERROR]);

	tuktest_catch_stdout_errors();

	if (tuktest_stats[TUKTEST_ERROR] > 0)
		return TUKTEST_EXIT_ERROR;

	if (tuktest_stats[TUKTEST_FAIL] > 0)
		return TUKTEST_EXIT_FAIL;

	if (tuktest_stats[TUKTEST_SKIP] > 0 || total_tests == 0)
		return TUKTEST_EXIT_SKIP;

	return TUKTEST_EXIT_PASS;
#endif
}


/// Runs the specified test function. Requires that tuktest_start()
/// has already been called and that tuktest_end() has NOT been called yet.
#define tuktest_run(testfunc) \
	tuktest_run_test(&(testfunc), #testfunc)

static void
tuktest_run_test(void (*testfunc)(void), const char *testfunc_str)
{
	// If any command line arguments were given, only the test functions
	// named on the command line will be run.
	if (tuktest_argc > 1) {
		int i = 1;
		while (strcmp(tuktest_argv[i], testfunc_str) != 0)
			if (++i == tuktest_argc)
				return;
	}

	// This is set so that failed assertions can print the correct
	// test name even when the assertion is in a helper function
	// called by the test function.
	tuktest_name = testfunc_str;

	// The way setjmp() may be called is very restrictive.
	// A switch statement is one of the few conforming ways
	// to get the value passed to longjmp(); doing something
	// like "int x = setjmp(env)" is NOT allowed (undefined behavior).
	switch (setjmp(tuktest_jmpenv)) {
		case 0:
			testfunc();
			++tuktest_stats[TUKTEST_PASS];
			if (!TUKTEST_QUIET)
				printf(TUKTEST_STR_PASS " %s\n", tuktest_name);
			break;

		case TUKTEST_FAIL:
			++tuktest_stats[TUKTEST_FAIL];
			break;

		case TUKTEST_SKIP:
			++tuktest_stats[TUKTEST_SKIP];
			break;

		default:
			++tuktest_stats[TUKTEST_ERROR];
			exit(tuktest_end());
	}

	tuktest_name = NULL;
}


// Internal helper that converts an enum tuktest_result value to a string.
static const char *
tuktest_result_str(enum tuktest_result result)
{
	return result == TUKTEST_PASS ? TUKTEST_STR_PASS
			: (result) == TUKTEST_FAIL ? TUKTEST_STR_FAIL
			: (result) == TUKTEST_SKIP ? TUKTEST_STR_SKIP
			: TUKTEST_STR_ERROR;
}


// Internal helper for assert_fail, assert_skip, and assert_error.
#define tuktest_print_and_jump(result, ...) \
do { \
	printf("%s %s [%s:%u] ", tuktest_result_str(result), tuktest_name, \
			tuktest_basename(__FILE__), __LINE__); \
	printf(__VA_ARGS__); \
	printf("\n"); \
	longjmp(tuktest_jmpenv, result); \
} while (0)


/// Unconditionally fails the test (non-zero exit status if not using TAP).
/// Execution will continue from the next test.
///
/// A printf format string is supported.
/// If no extra message is wanted, use "" as the argument.
#define assert_fail(...) tuktest_print_and_jump(TUKTEST_FAIL, __VA_ARGS__)


/// Skips the test (exit status 77 if not using TAP).
/// Execution will continue from the next test.
///
/// If you can detect early that no tests can be run, tuktest_early_skip()
/// might be a better way to skip the test(s). Especially in TAP mode this
/// makes a difference as with assert_skip() it will list a skipped specific
/// test name but with tuktest_early_skip() it will indicate that the whole
/// test program was skipped (with tuktest_early_skip() the TAP plan will
/// indicate zero tests).
///
/// A printf format string is supported.
/// If no extra message is wanted, use "" as the argument.
#define assert_skip(...) tuktest_print_and_jump(TUKTEST_SKIP, __VA_ARGS__)


/// Hard error (exit status 99 if not using TAP).
/// The remaining tests in this program will not be run or reported.
///
/// A printf format string is supported.
/// If no extra message is wanted, use "" as the argument.
#define assert_error(...) tuktest_print_and_jump(TUKTEST_ERROR, __VA_ARGS__)


/// Fails the test if the test expression doesn't evaluate to false.
#define assert_false(test_expr) \
do { \
	if (test_expr) \
		assert_fail("assert_fail: '%s' is true but should be false", \
				#test_expr); \
} while (0)


/// Fails the test if the test expression doesn't evaluate to true.
#define assert_true(test_expr) \
do { \
	if (!(test_expr)) \
		assert_fail("assert_true: '%s' is false but should be true", \
				#test_expr); \
} while (0)


/// Fails the test if comparing the signed integer expressions using the
/// specified comparison operator evaluates to false. For example,
/// assert_int(foobar(), >=, 0) fails the test if 'foobar() >= 0' isn't true.
/// For good error messages, the first argument should be the test expression
/// and the third argument the reference value (usually a constant).
///
/// For equality (==) comparison there is a assert_int_eq() which
/// might be more convenient to use.
#define assert_int(test_expr, cmp_op, ref_value) \
do { \
	const tuktest_int v_test_ = (test_expr); \
	const tuktest_int v_ref_ = (ref_value); \
	if (!(v_test_ cmp_op v_ref_)) \
		assert_fail("assert_int: '%s == %" TUKTEST_PRId \
				"' but expected '... %s %" TUKTEST_PRId "'", \
				#test_expr, v_test_, #cmp_op, v_ref_); \
} while (0)


/// Like assert_int() but for unsigned integers.
///
/// For equality (==) comparison there is a assert_uint_eq() which
/// might be more convenient to use.
#define assert_uint(test_expr, cmp_op, ref_value) \
do { \
	const tuktest_uint v_test_ = (test_expr); \
	const tuktest_uint v_ref_ = (ref_value); \
	if (!(v_test_ cmp_op v_ref_)) \
		assert_fail("assert_uint: '%s == %" TUKTEST_PRIu \
				"' but expected '... %s %" TUKTEST_PRIu "'", \
				#test_expr, v_test_, #cmp_op, v_ref_); \
} while (0)


/// Fails the test if test expression doesn't equal the expected
/// signed integer value.
#define assert_int_eq(test_expr, ref_value) \
	assert_int(test_expr, ==, ref_value)


/// Fails the test if test expression doesn't equal the expected
/// unsigned integer value.
#define assert_uint_eq(test_expr, ref_value) \
	assert_uint(test_expr, ==, ref_value)


/// Fails the test if the test expression doesn't equal the expected
/// enumeration value. This is like assert_int_eq() but the error message
/// shows the enumeration constant names instead of their numeric values
/// as long as the values are non-negative and not big.
///
/// The third argument must be a table of string pointers. A pointer to
/// a pointer doesn't work because this determines the number of elements
/// in the array using sizeof. For example:
///
///     const char *my_enum_names[] = { "MY_FOO", "MY_BAR", "MY_BAZ" };
///     assert_enum_eq(some_func_returning_my_enum(), MY_BAR, my_enum_names);
///
/// (If the reference value is out of bounds, both values are printed as
/// an integer. If only test expression is out of bounds, it is printed
/// as an integer and the reference as a string. Otherwise both are printed
/// as a string.)
#define assert_enum_eq(test_expr, ref_value, enum_strings) \
do { \
	const tuktest_int v_test_ = (test_expr); \
	const tuktest_int v_ref_ = (ref_value); \
	if (v_test_ != v_ref_) { \
		const int array_len_ = (int)(sizeof(enum_strings) \
				/ sizeof((enum_strings)[0])); \
		if (v_ref_ < 0 || v_ref_ >= array_len_) \
			assert_fail("assert_enum_eq: '%s == %" TUKTEST_PRId \
					"' but expected " \
					"'... == %" TUKTEST_PRId "'", \
					#test_expr, v_test_, v_ref_); \
		else if (v_test_ < 0 || v_test_ >= array_len_) \
			assert_fail("assert_enum_eq: '%s == %" TUKTEST_PRId \
					"' but expected '... == %s'", \
					#test_expr, v_test_, \
					(enum_strings)[v_ref_]); \
		else \
			assert_fail("assert_enum_eq: '%s == %s' " \
					"but expected '... = %s'", \
					#test_expr, (enum_strings)[v_test_], \
					(enum_strings)[v_ref_]); \
	} \
} while (0)


/// Fails the test if the specified bit isn't set in the test expression.
#define assert_bit_set(test_expr, bit) \
do { \
	const tuktest_uint v_test_ = (test_expr); \
	const unsigned v_bit_ = (bit); \
	const tuktest_uint v_mask_ = (tuktest_uint)1 << v_bit_; \
	if (!(v_test_ & v_mask_)) \
		assert_fail("assert_bit_set: '%s == 0x%" TUKTEST_PRIX \
				"' but bit %u (0x%" TUKTEST_PRIX ") " \
				"is not set", \
				#test_expr, v_test_, v_bit_, v_mask_); \
} while (0)


/// Fails the test if the specified bit is set in the test expression.
#define assert_bit_not_set(test_expr, bit) \
do { \
	const tuktest_uint v_test_ = (test_expr); \
	const unsigned v_bit_ = (bit); \
	const tuktest_uint v_mask_ = (tuktest_uint)1 << v_bit_; \
	if (v_test_ & v_mask_) \
		assert_fail("assert_bit_not_set: '%s == 0x%" TUKTEST_PRIX \
				"' but bit %u (0x%" TUKTEST_PRIX ") is set", \
				#test_expr, v_test_, v_bit_, v_mask_); \
} while (0)


/// Fails the test if unless all bits that are set in the bitmask are also
/// set in the test expression.
#define assert_bitmask_set(test_expr, mask) \
do { \
	const tuktest_uint v_mask_ = (mask); \
	const tuktest_uint v_test_ = (test_expr) & v_mask_; \
	if (v_test_ != v_mask_) \
		assert_fail("assert_bitmask_set: " \
				"'((%s) & 0x%" TUKTEST_PRIX ") == " \
				"0x%" TUKTEST_PRIX "' but expected " \
				"'... == 0x%" TUKTEST_PRIX "'", \
				#test_expr, v_mask_, v_test_, v_mask_); \
} while (0)


/// Fails the test if any of the bits that are set in the bitmask are also
/// set in the test expression.
#define assert_bitmask_not_set(test_expr, mask) \
do { \
	const tuktest_uint v_mask_ = (mask); \
	const tuktest_uint v_test_ = (test_expr) & v_mask_; \
	if (v_test_ != 0) \
		assert_fail("assert_bitmask_not_set: "\
				"'((%s) & 0x%" TUKTEST_PRIX ") == " \
				"0x%" TUKTEST_PRIX "' but expected " \
				"'... == 0'", \
				#test_expr, v_mask_, v_test_); \
} while (0)


// Internal helper to add common code for string assertions.
#define tuktest_str_helper1(macro_name, test_expr, ref_value) \
	const char *v_test_ = (test_expr); \
	const char *v_ref_ = (ref_value); \
	if (v_test_ == NULL) \
		assert_fail(macro_name ": Test expression '%s' is NULL", \
				#test_expr); \
	if (v_ref_ == NULL) \
		assert_fail(macro_name ": Reference value '%s' is NULL", \
				#ref_value)


// Internal helper to add common code for string assertions and to check
// that the reference value isn't an empty string.
#define tuktest_str_helper2(macro_name, test_expr, ref_value) \
	tuktest_str_helper1(macro_name, test_expr, ref_value); \
	if (v_ref_[0] == '\0') \
		assert_fail(macro_name ": Reference value is an empty string")


/// Fails the test if the test expression evaluates to string that doesn't
/// equal to the expected string.
#define assert_str_eq(test_expr, ref_value) \
do { \
	tuktest_str_helper1("assert_str_eq", test_expr, ref_value); \
	if (strcmp(v_ref_, v_test_) != 0) \
		assert_fail("assert_str_eq: '%s' evaluated to '%s' " \
				"but expected '%s'", \
				#test_expr, v_test_, v_ref_); \
} while (0)


/// Fails the test if the test expression evaluates to a string that doesn't
/// contain the reference value as a substring. Also fails the test if
/// the reference value is an empty string.
#define assert_str_contains(test_expr, ref_value) \
do { \
	tuktest_str_helper2("assert_str_contains", test_expr, ref_value); \
	if (strstr(v_test_, v_ref_) == NULL) \
		assert_fail("assert_str_contains: '%s' evaluated to '%s' " \
				"which doesn't contain '%s'", \
				#test_expr, v_test_, v_ref_); \
} while (0)


/// Fails the test if the test expression evaluates to a string that
/// contains the reference value as a substring. Also fails the test if
/// the reference value is an empty string.
#define assert_str_doesnt_contain(test_expr, ref_value) \
do { \
	tuktest_str_helper2("assert_str_doesnt_contain", \
			test_expr, ref_value); \
	if (strstr(v_test_, v_ref_) != NULL) \
		assert_fail("assert_str_doesnt_contain: " \
				"'%s' evaluated to '%s' which contains '%s'", \
				#test_expr, v_test_, v_ref_); \
} while (0)


/// Fails the test if the first array_size elements of the test array
/// don't equal to correct_array.
///
/// NOTE: This avoids %zu for portability to very old systems that still
/// can compile C99 code.
#define assert_array_eq(test_array, correct_array, array_size) \
do { \
	for (size_t i_ = 0; i_ < (array_size); ++i_) \
		if ((test_array)[i_] != (correct_array)[i_]) \
			assert_fail("assert_array_eq: " \
					"%s[%" TUKTEST_PRIu "] != "\
					"%s[%" TUKTEST_PRIu "] " \
					"but should be equal", \
					#test_array, (tuktest_uint)i_, \
					#correct_array, (tuktest_uint)i_); \
} while (0)

#endif
