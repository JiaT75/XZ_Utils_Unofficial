///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_mt_decoder.c
/// \brief      Tests the multithreaded decoder
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include "test_utils.h"
#include "src/liblzma/common/outqueue.h"


#define SLOW_INPUT_CHUNK_COUNT 10
#define BLOCK_SIZE 4096
#define PARTIAL_HEADERS_PATH "files/multithreaded/random_partial_headers"

// Struct taken from src/liblzma/common/stream_decoder_mt.c
// Needs to be updated here when this is updated in the decoder
// Since this is not in the header file, we cannot include it any
// other way. We need the internal values in order to test things
// like memory and thread usage
struct lzma_stream_coder {
	enum {
		SEQ_STREAM_HEADER,
		SEQ_BLOCK_HEADER,
		SEQ_BLOCK_INIT,
		SEQ_BLOCK_THR_INIT,
		SEQ_BLOCK_THR_RUN,
		SEQ_BLOCK_DIRECT_INIT,
		SEQ_BLOCK_DIRECT_RUN,
		SEQ_INDEX_WAIT_OUTPUT,
		SEQ_INDEX_DECODE,
		SEQ_STREAM_FOOTER,
		SEQ_STREAM_PADDING,
		SEQ_ERROR,
	} sequence;

	/// Block decoder
	lzma_next_coder block_decoder;

	/// Every Block Header will be decoded into this structure.
	/// This is also used to initialize a Block decoder when in
	/// direct mode. In threaded mode, a thread-specific copy will
	/// be made for decoder initialization because the Block decoder
	/// will modify the structure given to it.
	lzma_block block_options;

	/// Buffer to hold a filter chain for Block Header decoding and
	/// initialization. These are freed after successful Block decoder
	/// initialization or at stream_decoder_mt_end(). The thread-specific
	/// copy of block_options won't hold a pointer to filters[] after
	/// initialization.
	lzma_filter filters[LZMA_FILTERS_MAX + 1];

	/// Stream Flags from Stream Header
	lzma_stream_flags stream_flags;

	/// Index is hashed so that it can be compared to the sizes of Blocks
	/// with O(1) memory usage.
	lzma_index_hash *index_hash;


	/// Maximum wait time if cannot use all the input and cannot
	/// fill the output buffer. This is in milliseconds.
	uint32_t timeout;


	/// Error code from a worker thread.
	///
	/// \note       Use mutex.
	lzma_ret thread_error;

	/// Error code to return after pending output has been copied out. If
	/// set in read_output_and_wait(), this is a mirror of thread_error.
	/// If set in stream_decode_mt() then it's, for example, error that
	/// occurred when decoding Block Header.
	lzma_ret pending_error;

	/// Number of threads that will be created at maximum.
	uint32_t threads_max;

	/// Number of thread structures that have been initialized from
	/// "threads", and thus the number of worker threads actually
	/// created so far.
	uint32_t threads_initialized;

	/// Array of allocated thread-specific structures. When no threads
	/// are in use (direct mode) this is NULL. In threaded mode this
	/// points to an array of threads_max number of worker_thread structs.
	struct worker_thread *threads;

	/// Stack of free threads. When a thread finishes, it puts itself
	/// back into this stack. This starts as empty because threads
	/// are created only when actually needed.
	///
	/// \note       Use mutex.
	struct worker_thread *threads_free;

	/// The most recent worker thread to which the main thread writes
	/// the new input from the application.
	struct worker_thread *thr;

	/// Output buffer queue for decompressed data from the worker threads
	///
	/// \note       Use mutex with operations that need it.
	lzma_outq outq;

	mythread_mutex mutex;
	mythread_cond cond;


	/// Memory usage that will not be exceeded in multi-threaded mode.
	/// Single-threaded mode can exceed this even by a large amount.
	uint64_t memlimit_threading;

	/// Memory usage limit that should never be exceeded.
	/// LZMA_MEMLIMIT_ERROR will be returned if decoding isn't possible
	/// even in single-threaded mode without exceeding this limit.
	uint64_t memlimit_stop;

	/// Amount of memory in use by the direct mode decoder
	/// (coder->block_decoder). In threaded mode this is 0.
	uint64_t mem_direct_mode;

	/// Amount of memory needed by the running worker threads.
	/// This doesn't include the memory needed by the output buffer.
	///
	/// \note       Use mutex.
	uint64_t mem_in_use;

	/// Amount of memory used by the idle (cached) threads.
	///
	/// \note       Use mutex.
	uint64_t mem_cached;


	/// Amount of memory needed for the filter chain of the next Block.
	uint64_t mem_next_filters;

	/// Amount of memory needed for the thread-specific input buffer
	/// for the next Block.
	uint64_t mem_next_in;

	/// Amount of memory actually needed to decode the next Block
	/// in threaded mode. This is
	/// mem_next_filters + mem_next_in + memory needed for lzma_outbuf.
	uint64_t mem_next_block;


	/// Amount of compressed data in Stream Header + Blocks that have
	/// already been finished.
	///
	/// \note       Use mutex.
	uint64_t progress_in;

	/// Amount of uncompressed data in Blocks that have already
	/// been finished.
	///
	/// \note       Use mutex.
	uint64_t progress_out;


	/// If true, LZMA_NO_CHECK is returned if the Stream has
	/// no integrity check.
	bool tell_no_check;

	/// If true, LZMA_UNSUPPORTED_CHECK is returned if the Stream has
	/// an integrity check that isn't supported by this liblzma build.
	bool tell_unsupported_check;

	/// If true, LZMA_GET_CHECK is returned after decoding Stream Header.
	bool tell_any_check;

	/// If true, we will tell the Block decoder to skip calculating
	/// and verifying the integrity check.
	bool ignore_check;

	/// If true, we will decode concatenated Streams that possibly have
	/// Stream Padding between or after them. LZMA_STREAM_END is returned
	/// once the application isn't giving us any new input (LZMA_FINISH),
	/// and we aren't in the middle of a Stream, and possible
	/// Stream Padding is a multiple of four bytes.
	bool concatenated;

	/// When decoding concatenated Streams, this is true as long as we
	/// are decoding the first Stream. This is needed to avoid misleading
	/// LZMA_FORMAT_ERROR in case the later Streams don't have valid magic
	/// bytes.
	bool first_stream;


	/// Write position in buffer[] and position in Stream Padding
	size_t pos;

	/// Buffer to hold Stream Header, Block Header, and Stream Footer.
	/// Block Header has biggest maximum size.
	uint8_t buffer[LZMA_BLOCK_HEADER_SIZE_MAX];
};

// These three test data sets are used as globals since they are
// used frequently across multiple tests
static test_file_data abc_data = {
	.compressed_filename = "files/multithreaded/abc.xz",
	.plain_filename = "files/multithreaded/abc"
};

static test_file_data text_data = {
	.compressed_filename = "files/multithreaded/text.xz",
	.plain_filename = "files/multithreaded/text"
};

static test_file_data random_data = {
	.compressed_filename = "files/multithreaded/random.xz",
	.plain_filename = "files/multithreaded/random"
};


// Test mt_decoder with most simple options
static void
basic_test(test_file_data *data)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(data->plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = data->compressed_size;
	strm.avail_out = data->plain_size;
	strm.next_in = data->compressed_data;
	strm.next_out = output_buf;

	// No timeout is set so lzma_code should finish everything
	// in one call because all of the input is provided up front
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	assert_ulong_equal(data->plain_size, strm.total_out);
	assert_n_array_equal(data->plain_data, output_buf,
			data->plain_size);
	lzma_end(&strm);
	free(output_buf);
}


// Test basic functionality across three types of data sets
static void
test_basic_mt_decoder(void)
{
	basic_test(&abc_data);
	basic_test(&text_data);
	basic_test(&random_data);
}


static void
decode_expect_broken(test_file_data *input_data, uint8_t * output,
		size_t *out_len, bool mt)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	uint64_t memlimit = lzma_physmem() / 2;
	assert_true(memlimit > 0);

	if (mt) {
		lzma_mt options = {
			.flags = 0,
			.threads = 4,
			.timeout = 0,
			.memlimit_threading = UINT64_MAX,
			.memlimit_stop = memlimit
		};

		assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm,
				&options));
	}
	else {
		assert_int_equal(LZMA_OK, lzma_stream_decoder(&strm,
				memlimit, 0));
	}

	strm.avail_in = input_data->compressed_size;
	strm.avail_out = *out_len;
	strm.next_in = input_data->compressed_data;
	strm.next_out = output;

	while (1) {
		lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
		// Expecting error, so failing to detect it
		// would be a problem
		assert_true(ret != LZMA_STREAM_END);

		if (ret != LZMA_OK) {
			break;
		}
	}

	lzma_end(&strm);
	*out_len = strm.total_out;
}


// This test is to ensure the multithreaded decoder
// produces as much output as the single threaded version
// when the input is corrupted
static void
test_broken_input(void)
{
	test_file_data random_truncated;
	random_truncated.compressed_filename = "files/multithreaded/"
				"random_corrupt_truncated.xz";
	random_truncated.plain_filename = NULL;

	test_file_data random_corrupted;
	random_corrupted.compressed_filename = "files/multithreaded/"
				"random_corrupt_contents.xz";
	random_corrupted.plain_filename = NULL;

	assert_true(prepare_test_file_data(&random_truncated));
	assert_true(prepare_test_file_data(&random_corrupted));

	uint8_t *output_mt = malloc(random_data.plain_size);
	size_t output_size_mt = random_data.plain_size;

	uint8_t *output_st = malloc(random_data.plain_size);
	size_t output_size_st = random_data.plain_size;

	decode_expect_broken(&random_truncated, output_mt,
			&output_size_mt, true);
	decode_expect_broken(&random_truncated, output_st,
			&output_size_st, false);

	assert_int_equal(output_size_st, output_size_mt);
	assert_n_array_equal(output_st, output_st, output_size_st);

	memzero(output_mt, output_size_mt);
	memzero(output_st, output_size_st);

	output_size_mt = random_data.plain_size;
	output_size_st = random_data.plain_size;

	decode_expect_broken(&random_corrupted, output_mt,
			&output_size_mt, true);
	decode_expect_broken(&random_corrupted, output_st,
			&output_size_st, false);

	assert_int_equal(output_size_st, output_size_mt);
	assert_n_array_equal(output_st, output_st, output_size_st);

	free(output_mt);
	free(output_st);
	free_test_file_data(&random_truncated);
	free_test_file_data(&random_corrupted);
}


// Test the memlimit_threading option to be sure
// that low limits forces single threaded mode
// but does not stop the decoding
static void
test_memlimit_threading(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	// Create a memlimit of 100 bytes to force single threaded
	const uint64_t memlimit_low = 100;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = memlimit_low
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(random_data.plain_size);
	assert_true(output_buf != NULL);

	// Read in the first 100 bytes of the stream
	// Then check if the sequence is SEQ_BLOCK_DIRECT_RUN
	// This will guarentee the decoder is running in
	// single threaded mode
	strm.avail_in = LZMA_STREAM_HEADER_SIZE + 100;
	strm.avail_out = random_data.plain_size;
	strm.next_in = random_data.compressed_data;
	strm.next_out = output_buf;

	lzma_ret ret = lzma_code(&strm, LZMA_RUN);
	assert_int_equal(LZMA_OK, ret);

	struct lzma_stream_coder *coder = (struct lzma_stream_coder *)
			 strm.internal->next.coder;

	assert_int_equal(SEQ_BLOCK_DIRECT_RUN, coder->sequence);

	// Decode the rest of the stream to test if the single
	// threaded mode works properly
	strm.avail_in = random_data.compressed_size - strm.total_in;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
}


/*
static void
test_adjust_memlimit_threading(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	// Create a memlimit of 100 bytes to force single threaded
	const uint64_t memlimit_low = 100;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = memlimit_low
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(random_data.plain_size);
	assert_true(output_buf != NULL);

	// Read in the first 100 bytes of the stream after the header
	// Then check if the sequence is SEQ_BLOCK_DIRECT_RUN
	// This will guarentee the decoder is running in
	// single threaded mode
	strm.avail_in = LZMA_STREAM_HEADER_SIZE + 100;
	strm.avail_out = random_data.plain_size;
	strm.next_in = random_data.compressed_data;
	strm.next_out = output_buf;

	lzma_ret ret = lzma_code(&strm, LZMA_RUN);
	assert_int_equal(LZMA_OK, ret);

	struct lzma_stream_coder *coder = (struct lzma_stream_coder *)
			 strm.internal->next.coder;

	assert_int_equal(SEQ_BLOCK_DIRECT_RUN, coder->sequence);

	assert_int_equal(LZMA_OK, lzma_memlimit_threading_set(
			&strm, options.memlimit_stop));

	// Compress a few more bytes and check if it is
	// back to multi threaded mode
	strm.avail_in = BLOCK_SIZE;
	assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));
	assert_int_equal(SEQ_BLOCK_THR_RUN, coder->sequence);

	// Decode the rest of the stream
	strm.avail_in = random_data.compressed_size - strm.total_in;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
}
*/


// Test the memlimit_stop option to be sure
// that low limits forces the decoding to stop
// and high limits accurately allow decoding
// to happen
static void
test_memlimit_stop(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	// Create a memlimit of 100 bytes to force single threaded
	const uint64_t memlimit_low = 100;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = memlimit_low,
		.memlimit_stop = memlimit_low
	};

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(random_data.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = LZMA_STREAM_HEADER_SIZE + 100;
	strm.avail_out = random_data.plain_size;
	strm.next_in = random_data.compressed_data;
	strm.next_out = output_buf;

	lzma_ret ret = lzma_code(&strm, LZMA_RUN);
	assert_int_equal(LZMA_MEMLIMIT_ERROR, ret);

	uint64_t new_memlimit = lzma_physmem() / 2;
	assert_true(new_memlimit > 0);

	lzma_memlimit_set(&strm, new_memlimit);

	ret = lzma_code(&strm, LZMA_RUN);
	assert_int_equal(LZMA_OK, ret);

	struct lzma_stream_coder *coder = (struct lzma_stream_coder *)
			 strm.internal->next.coder;

	// Since memlimit_threading was not updated, should be using
	// single threaded mode
	assert_int_equal(SEQ_BLOCK_DIRECT_RUN, coder->sequence);

	// Decode the rest of the input
	strm.avail_in = random_data.compressed_size - strm.total_in;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	free(output_buf);
	lzma_end(&strm);
}


static void
test_tell_no_check(void)
{
	static test_file_data text_data_no_check = {
		.compressed_filename = "files/multithreaded/text_no_check.xz",
		.plain_filename = "files/multithreaded/text"
	};

	assert_true(prepare_test_file_data(&text_data_no_check));

	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = LZMA_TELL_NO_CHECK,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX,
		.memlimit_stop = UINT64_MAX
	};

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(text_data_no_check.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = text_data_no_check.compressed_size;
	strm.avail_out = text_data_no_check.plain_size;
	strm.next_in = text_data_no_check.compressed_data;
	strm.next_out = output_buf;

	assert_int_equal(LZMA_NO_CHECK, lzma_code(&strm, LZMA_RUN));

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	assert_ulong_equal(text_data_no_check.plain_size, strm.total_out);
	assert_n_array_equal(text_data_no_check.plain_data, output_buf,
			text_data_no_check.plain_size);
	free(output_buf);
	lzma_end(&strm);
	free_test_file_data(&text_data_no_check);
}


static void
test_tell_unsupported_check(void)
{
	static test_file_data text_data_unsupported_check = {
		.compressed_filename =
			"files/multithreaded/text_unsupported_check.xz",
		.plain_filename = "files/multithreaded/text"
	};

	assert_true(prepare_test_file_data(&text_data_unsupported_check));

	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = LZMA_TELL_UNSUPPORTED_CHECK,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX,
		.memlimit_stop = UINT64_MAX
	};

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	uint8_t *output_buf = (uint8_t *) malloc(text_data_unsupported_check.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = text_data_unsupported_check.compressed_size;
	strm.avail_out = text_data_unsupported_check.plain_size;
	strm.next_in = text_data_unsupported_check.compressed_data;
	strm.next_out = output_buf;

	assert_int_equal(LZMA_UNSUPPORTED_CHECK, lzma_code(&strm, LZMA_RUN));

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	assert_ulong_equal(text_data_unsupported_check.plain_size, strm.total_out);
	assert_n_array_equal(text_data_unsupported_check.plain_data, output_buf,
			text_data_unsupported_check.plain_size);

	free(output_buf);
	lzma_end(&strm);
	free_test_file_data(&text_data_unsupported_check);
}


static void
test_tell_any_check(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = LZMA_TELL_ANY_CHECK,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX,
		.memlimit_stop = UINT64_MAX
	};

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	uint8_t *output_buf = (uint8_t *) malloc(text_data.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = text_data.compressed_size;
	strm.avail_out = text_data.plain_size;
	strm.next_in = text_data.compressed_data;
	strm.next_out = output_buf;

	assert_int_equal(LZMA_GET_CHECK, lzma_code(&strm, LZMA_RUN));

	assert_int_equal(LZMA_CHECK_CRC64, lzma_get_check(&strm));

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	assert_ulong_equal(text_data.plain_size, strm.total_out);
	assert_n_array_equal(text_data.plain_data, output_buf,
			text_data.plain_size);
	free(output_buf);
	lzma_end(&strm);
}


static void
test_concatenated(void)
{
	// Combined consists of test, abc, and random concatented together
	static test_file_data combined_data = {
		.compressed_filename = "files/multithreaded/combined.xz"
	};

	assert_true(prepare_test_file_data(&combined_data));

	size_t combined_plaintext_size = text_data.plain_size +
			abc_data.plain_size + random_data.plain_size;

	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = LZMA_CONCATENATED,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX,
		.memlimit_stop = UINT64_MAX
	};

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	uint8_t *output_buf = (uint8_t *) malloc(combined_plaintext_size);
	assert_true(output_buf != NULL);

	strm.avail_in = combined_data.compressed_size;
	strm.avail_out = combined_plaintext_size;
	strm.next_in = combined_data.compressed_data;
	strm.next_out = output_buf;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	uint8_t *combined_plain_data = malloc(combined_plaintext_size);
	assert_true(combined_plain_data != NULL);
	memcpy(combined_plain_data, text_data.plain_data, text_data.plain_size);
	memcpy(combined_plain_data + text_data.plain_size,
			abc_data.plain_data, abc_data.plain_size);
	memcpy(combined_plain_data + text_data.plain_size +
			abc_data.plain_size, random_data.plain_data,
			random_data.plain_size);

	assert_ulong_equal(combined_plaintext_size, strm.total_out);
	assert_n_array_equal(combined_plain_data, output_buf,
			combined_plaintext_size);

	free(output_buf);
	free(combined_plain_data);
	lzma_end(&strm);
	free_test_file_data(&combined_data);
}


// Tests different combinations of the decoder flags:
// LZMA_TELL_NO_CHECK
// LZMA_TELL_UNSUPPORTED_CHECK
// LZMA_TELL_ANY_CHECK
// LZMA_CONCATENATED
static void
test_flags(void)
{
	test_tell_no_check();
	test_tell_unsupported_check();
	test_tell_any_check();
	test_concatenated();
}


static void
test_large_timeout(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = UINT32_MAX,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(random_data.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = random_data.compressed_size;
	strm.avail_out = random_data.plain_size;
	strm.next_in = random_data.compressed_data;
	strm.next_out = output_buf;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
}


static void
test_small_timeout(void)
{
	static test_file_data large_random_data = {
		.compressed_filename =
				"files/multithreaded/large_random.xz",
		.plain_filename = "files/multithreaded/large_random"
	};

	prepare_test_file_data(&large_random_data);

	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 1,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(large_random_data.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = large_random_data.compressed_size;
	strm.avail_out = large_random_data.plain_size;
	strm.next_in = large_random_data.compressed_data;
	strm.next_out = output_buf;

	lzma_action action = LZMA_RUN;

	// Timeout should occur before decoding is done
	// so, we should not have LZMA_STREAM_END returned
	assert_int_equal(LZMA_OK, lzma_code(&strm, action));

	// Loop until decoding is done since it should timeout
	// a few times
	while (1) {
		if (strm.avail_in == 0) {
			action = LZMA_FINISH;
		}

		lzma_ret ret = lzma_code(&strm, action);

		if (ret == LZMA_STREAM_END) {
			break;
		}

		assert_int_equal(ret, LZMA_OK);
	}

	assert_ulong_equal(large_random_data.plain_size, strm.total_out);
	assert_n_array_equal(large_random_data.plain_data, output_buf,
			large_random_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
	free_test_file_data(&large_random_data);
}

// Provide a large input file and a low timeout
// to force the timeout to occur before the decoding
// is complete
// Also provide a large input file and a large timeout
// to be sure the timeout does not stop decoding when
// it shouldn't
static void
test_timeout(void)
{
	test_large_timeout();
	test_small_timeout();
}


// Testing short timeouts in between providing input/output to the main
// thread in order to look for timing and race condition bugs
// This is made generic by the input parameter which determines if the
// input (true) or the output (false) should be slowly rolled out
// The other buffer will be provided in full from the start
static void
test_slow(bool input)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(text_data.plain_size);
	assert_true(output_buf != NULL);

	uint64_t chunk_size = 0;

	if (input) {
		chunk_size = text_data.compressed_size
			 / SLOW_INPUT_CHUNK_COUNT;
		strm.avail_out = text_data.plain_size;
	}
	else {
		chunk_size = text_data.plain_size
			 / SLOW_INPUT_CHUNK_COUNT;
		strm.avail_in = text_data.compressed_size;
	}

	strm.next_in = text_data.compressed_data;
	strm.next_out = output_buf;

	for (int i = 0; i < SLOW_INPUT_CHUNK_COUNT; i++) {
		if (input)
			strm.avail_in = chunk_size;
		else
			strm.avail_out = chunk_size;

		assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));

		if (input)
			assert_int_equal(0, strm.avail_in);
		else
			assert_int_equal(0, strm.avail_out);

		sleep_ms(100);
	}

	if (input)
		strm.avail_in = text_data.compressed_size
				% SLOW_INPUT_CHUNK_COUNT;
	else
		strm.avail_out = text_data.plain_size
				% SLOW_INPUT_CHUNK_COUNT;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	assert_ulong_equal(text_data.plain_size, strm.total_out);
	assert_n_array_equal(text_data.plain_data, output_buf,
			text_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
}


static void
test_slow_input(void)
{
	test_slow(true);
}


static void
test_slow_output(void)
{
	test_slow(false);
}


// Tests if the uncompressed and compressed sizes are not included
// in the block header then single threaded decoding is useds
static void
test_no_size_in_headers(void)
{
	// File created with single threaded xz, so the block size is
	// not stored in the headers
	static test_file_data no_size_in_headers = {
		.compressed_filename = "files/multithreaded/random_st.xz",
		.plain_filename = "files/multithreaded/random"
	};

	assert_true(prepare_test_file_data(&no_size_in_headers));

	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(no_size_in_headers.plain_size);
	assert_true(output_buf != NULL);

	// Read in the first 100 bytes of the stream
	// Then check if the sequence is SEQ_BLOCK_DIRECT_RUN
	// This will guarentee the decoder is running in
	// single threaded mode
	strm.avail_in = LZMA_STREAM_HEADER_SIZE + 100;
	strm.avail_out = no_size_in_headers.plain_size;
	strm.next_in = no_size_in_headers.compressed_data;
	strm.next_out = output_buf;

	lzma_ret ret = lzma_code(&strm, LZMA_RUN);
	assert_int_equal(LZMA_OK, ret);

	struct lzma_stream_coder *coder = (struct lzma_stream_coder *)
			 strm.internal->next.coder;

	assert_int_equal(SEQ_BLOCK_DIRECT_RUN, coder->sequence);

	// Decode the rest of the stream to test if the single
	// threaded mode works properly
	strm.avail_in = no_size_in_headers.compressed_size - strm.total_in;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	assert_ulong_equal(no_size_in_headers.plain_size, strm.total_out);
	assert_n_array_equal(no_size_in_headers.plain_data, output_buf,
			no_size_in_headers.plain_size);
	lzma_end(&strm);
	free(output_buf);
	free_test_file_data(&no_size_in_headers);
}


// This function tests the multithreaded decoders ability to
// properly switch between single and multithreaded mode in
// the same xz stream.
// The test file includes 7 blocks in which the first two and
// the last block can be decoded multithreaded and the middle 4
// blocks cannot:
// * Blocks 1-2 include uncompressed and compressed sizes in their headers
// * Blocks 3-4 do not inlcude sizes in their headers
// * Block 5 includes only the uncompressed size
// * Block 6 includes only the compressed size
// * Block 7 includes uncompressed and compressed sizes in its header
// The code that generated this file is included in
// three functions following this test case.
static void
test_partial_size_in_headers(void)
{
	static test_file_data partial_headers_data = {
		.compressed_filename = PARTIAL_HEADERS_PATH ".xz",
		.plain_filename = PARTIAL_HEADERS_PATH
	};
	assert_true(prepare_test_file_data(&partial_headers_data));

	lzma_stream strm = LZMA_STREAM_INIT;

	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.timeout = 0,
		.memlimit_threading = UINT64_MAX
	};

	options.memlimit_stop = lzma_physmem() / 2;
	assert_true(options.memlimit_stop > 0);

	assert_int_equal(LZMA_OK, lzma_stream_decoder_mt(&strm, &options));

	// Create buffer to hold output data
	uint8_t *output_buf = (uint8_t *) malloc(
			partial_headers_data.plain_size);
	assert_true(output_buf != NULL);

	strm.avail_in = LZMA_STREAM_HEADER_SIZE + BLOCK_SIZE / 2;
	strm.avail_out = partial_headers_data.plain_size;
	strm.next_in = partial_headers_data.compressed_data;
	strm.next_out = output_buf;

	struct lzma_stream_coder *coder = (struct lzma_stream_coder *)
			strm.internal->next.coder;

	const bool expected_mt[7] = {
		true, true, false, false, false, false, true
	};

	// Decode each block (7 total) and determine if the decoder
	// is multithreaded or single threaded
	for (int i = 0; i < 7; i++) {
		// Decode into the middle of the block
		assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));

		if (expected_mt[i])
			assert_int_equal(SEQ_BLOCK_THR_RUN,
					coder->sequence);
		else
			assert_int_equal(SEQ_BLOCK_DIRECT_RUN,
					coder->sequence);

		// Prepare to decode into the middle of the next block
		strm.avail_in = BLOCK_SIZE;
	}

	// Finish decoding until the end
	strm.avail_in = partial_headers_data.compressed_size - strm.total_in;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	// Verify the contents match the original
	assert_int_equal(partial_headers_data.plain_size, strm.total_out);
	assert_n_array_equal(partial_headers_data.plain_data, output_buf,
			strm.total_out);
	// Cleanup
	free(output_buf);
	free_test_file_data(&partial_headers_data);
}

/*
* The next three functions below were used to generate
* files/multithreaded/random_partial_headers in order to test
* if the decoder correctly switches from single threaded to
* multithreaded mode. It is left here for reference since
* test_partial_size_in_headers needs to know which blocks
* should be multi threaded decoded and which shouldn't
* In the future, these functions can be more generalized
* and moved to the debug folder

static void
encode_block_without_sizes_in_header(uint8_t* output, size_t* out_pos,
		size_t out_size, uint8_t** input, lzma_filter* filters,
		lzma_index *idx)
{
	// Cannot use single call because that sets the compressed
	// and uncompressed sizes in the header
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_block block = {
		.check = LZMA_CHECK_CRC32,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.filters = filters
	};

	assert_int_equal(LZMA_OK, lzma_block_encoder(&strm,
			&block));
	// First, write out the block header because we do not want
	// to include the size fields
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	assert_int_equal(LZMA_OK, lzma_block_header_encode(&block,
			output + *out_pos));
	*out_pos += block.header_size;
	// Next encode the block, block padding, and check
	strm.avail_in = BLOCK_SIZE;
	strm.avail_out = out_size;
	strm.next_in = *input;
	strm.next_out = output + *out_pos;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);
	*input += BLOCK_SIZE;
	*out_pos += strm.total_out;

	assert_int_equal(LZMA_OK, lzma_index_append(idx, NULL,
			block.header_size + strm.total_out, BLOCK_SIZE));

	lzma_end(&strm);
}


static void
encode_block_with_one_size_in_header(uint8_t* output, size_t* out_pos,
		size_t out_size, uint8_t** input, lzma_filter* filters,
		bool set_compressed_size, lzma_index *idx)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_block block = {
		.check = LZMA_CHECK_CRC32,
		.compressed_size = LZMA_VLI_UNKNOWN,
		.uncompressed_size = LZMA_VLI_UNKNOWN,
		.filters = filters
	};

	if (set_compressed_size) {
		block.compressed_size = lzma_block_buffer_bound(BLOCK_SIZE);
	}
	else {
		block.uncompressed_size = BLOCK_SIZE;
	}

	assert_int_equal(LZMA_OK, lzma_block_encoder(&strm,
			&block));
	// Make space for header, but do not encode yet
	assert_int_equal(LZMA_OK, lzma_block_header_size(&block));
	uint8_t* block_header_location = output + *out_pos;

	*out_pos += block.header_size;
	// Next encode the block, block padding, and check
	strm.avail_in = BLOCK_SIZE;
	strm.avail_out = out_size;
	strm.next_in = *input;
	strm.next_out = output + *out_pos;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));
	assert_int_equal(0, strm.avail_in);

	if (set_compressed_size) {
		block.uncompressed_size = LZMA_VLI_UNKNOWN;
		assert_int_equal(LZMA_OK, lzma_block_header_encode(&block,
			block_header_location));
	}
	else {
		block.compressed_size = LZMA_VLI_UNKNOWN;
		assert_int_equal(LZMA_OK, lzma_block_header_encode(&block,
			block_header_location));
	}

	*input += BLOCK_SIZE;
	*out_pos += strm.total_out;

	assert_int_equal(LZMA_OK, lzma_index_append(idx, NULL,
			block.header_size + strm.total_out, BLOCK_SIZE));

	lzma_end(&strm);
}


static void
generate_partial_size_in_headers(void)
{
	uint8_t* input = NULL;
	size_t file_size = read_file_into_buffer(
			"files/multithreaded/random", &input);
	assert_true(file_size > 0);

	// Create output buffer
	size_t output_size = lzma_block_buffer_bound(file_size);
	uint8_t* output = malloc(output_size);
	assert_true(output != NULL);

	// Setup the filters
	lzma_options_lzma opt_lzma;
	assert_true(!lzma_lzma_preset(&opt_lzma, 1));

	lzma_filter filters[] = {
		{
			.id = LZMA_FILTER_LZMA2,
			.options = &opt_lzma
		},
		{
			.id = LZMA_VLI_UNKNOWN
		}
	};

	lzma_block first_block = {
		.check = LZMA_CHECK_CRC32,
		.compressed_size = lzma_block_buffer_bound(BLOCK_SIZE),
		.uncompressed_size = BLOCK_SIZE,
		.filters = filters
	};

	lzma_stream_flags sf = {
		.check = first_block.check
	};

	// Encode stream header
	size_t out_pos = 0;
	assert_int_equal(LZMA_OK, lzma_stream_header_encode(&sf, output));
	out_pos += LZMA_STREAM_HEADER_SIZE;

	uint8_t* input_ptr = input;

	// Create the index
	lzma_index *idx = lzma_index_init(NULL);
	assert(idx != NULL);

	// Encode block 1 with sizes in header
	size_t output_pos_before = out_pos;
	assert_int_equal(LZMA_OK, lzma_block_buffer_encode(&first_block,
			NULL, input_ptr, BLOCK_SIZE, output, &out_pos,
			output_size));
	input_ptr += BLOCK_SIZE;
	assert_int_equal(LZMA_OK, lzma_index_append(idx, NULL,
			lzma_block_unpadded_size(&first_block),
			BLOCK_SIZE));

	// Encode block 2 with sizes in header
	lzma_block second_block = {
		.check = LZMA_CHECK_CRC32,
		.compressed_size = lzma_block_buffer_bound(BLOCK_SIZE),
		.uncompressed_size = BLOCK_SIZE,
		.filters = filters
	};

	output_pos_before = out_pos;
	assert_int_equal(LZMA_OK, lzma_block_buffer_encode(&second_block,
			NULL, input_ptr, BLOCK_SIZE, output, &out_pos,
			output_size - out_pos));
	input_ptr += BLOCK_SIZE;
	assert_int_equal(LZMA_OK, lzma_index_append(idx, NULL,
			out_pos - output_pos_before, BLOCK_SIZE));

	// Encode block 3 without sizes in header
	encode_block_without_sizes_in_header(output, &out_pos,
			output_size - out_pos, &input_ptr, filters, idx);

	// Encode block 4 without sizes in header
	encode_block_without_sizes_in_header(output, &out_pos,
			output_size - out_pos, &input_ptr, filters, idx);

	// Encode block 5 with only uncompressed size set
	encode_block_with_one_size_in_header(output, &out_pos,
			output_size - out_pos, &input_ptr, filters,
			false, idx);

	// Encode block 6 with only compressed size set
	encode_block_with_one_size_in_header(output, &out_pos,
			output_size - out_pos, &input_ptr, filters,
			true, idx);

	// Encode block 7 with sizes in header
	lzma_block seventh_block = {
		.check = LZMA_CHECK_CRC32,
		.compressed_size = lzma_block_buffer_bound(BLOCK_SIZE),
		.uncompressed_size = BLOCK_SIZE,
		.filters = filters
	};

	output_pos_before = out_pos;
	assert_int_equal(LZMA_OK, lzma_block_buffer_encode(&seventh_block,
			NULL, input_ptr, BLOCK_SIZE, output, &out_pos,
			output_size - out_pos));
	input_ptr += BLOCK_SIZE;
	assert_int_equal(LZMA_OK, lzma_index_append(idx, NULL,
			out_pos - output_pos_before, BLOCK_SIZE));

	// Encode index
	lzma_stream index_strm = LZMA_STREAM_INIT;
	assert_int_equal(LZMA_OK, lzma_index_encoder(&index_strm, idx));

	index_strm.avail_in = BLOCK_SIZE;
	index_strm.avail_out = output_size - out_pos;
	index_strm.next_in = input_ptr;
	index_strm.next_out = output + out_pos;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&index_strm, LZMA_RUN));
	out_pos += index_strm.total_out;

	lzma_end(&index_strm);
	lzma_index_end(idx, NULL);

	// Encoder stream footer
	sf.backward_size = index_strm.total_out;
	assert_int_equal(LZMA_OK, lzma_stream_footer_encode(&sf,
			output + out_pos));
	out_pos += LZMA_STREAM_HEADER_SIZE;

	// Write out the file
	FILE* outfile = fopen(
			PARTIAL_HEADERS_PATH ".xz",
			"w");
	assert_true(outfile != NULL);
	assert_int_equal(out_pos, fwrite(output, 1, out_pos, outfile));
	assert_int_equal(0, fclose(outfile));

	// Clean up
	free(input);
	free(output);
}
*/

void
test_mt_decoder(void)
{
	assert_true(prepare_test_file_data(&abc_data));
	assert_true(prepare_test_file_data(&text_data));
	assert_true(prepare_test_file_data(&random_data));

	test_fixture_start();
	run_test(test_basic_mt_decoder);
	run_test(test_broken_input);
	run_test(test_memlimit_threading);
	//run_test(test_adjust_memlimit_threading);
	run_test(test_memlimit_stop);
	run_test(test_flags);
	run_test(test_timeout);
	run_test(test_slow_input);
	run_test(test_slow_output);
	run_test(test_no_size_in_headers);
	run_test(test_partial_size_in_headers);
	test_fixture_end();

	free_test_file_data(&abc_data);
	free_test_file_data(&text_data);
	free_test_file_data(&random_data);
}
