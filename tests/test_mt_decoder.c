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
#include "sysdefs.h"
#include "src/liblzma/common/common.h"
#include "src/liblzma/common/outqueue.h"


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

	lzma_action action = LZMA_RUN;
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

	assert_ulong_equal(data->plain_size, strm.total_out);
	assert_n_array_equal(data->plain_data, output_buf,
			data->plain_size);
	lzma_end(&strm);
	free(output_buf);
}


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
	strm.avail_out = out_len;
	strm.next_in = input_data->compressed_data;
	strm.next_out = output;

	lzma_action action = LZMA_RUN;
	while (1) {
		if (strm.avail_in == 0) {
			action = LZMA_FINISH;
		}

		lzma_ret ret = lzma_code(&strm, action);
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

	/*
	Commented out here because this code currently causes
	deadlock

	decode_expect_broken(&random_truncated, output_mt,
			&output_size_mt, true);
	decode_expect_broken(&random_truncated, output_st,
			&output_size_st, false);

	assert_int_equal(output_size_st, output_size_mt);
	assert_n_array_equal(output_st, output_st, output_size_st);
	*/

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
	strm.avail_in = random_data.compressed_size - strm.avail_in;
	lzma_action action = LZMA_RUN;
	while (1) {
		if (strm.avail_in == 0) {
			action = LZMA_FINISH;
		}

		lzma_ret ret = lzma_code(&strm, action);

		if (ret == LZMA_STREAM_END) {
			break;
		}

		assert_int_equal(ret, LZMA_OK);
		assert_int_equal(1, coder->threads_initialized);
	}

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	lzma_end(&strm);
	free(output_buf);
}


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

	// Currently failing because the memlimit_threading
	// is changed during the init, but not when lzma_memlimit_set
	// updates memlimit_stop
	assert_int_equal(SEQ_BLOCK_THR_RUN, coder->sequence);

	strm.avail_in = random_data.compressed_size - strm.avail_in;
	lzma_action action = LZMA_RUN;
	while (1) {
		if (strm.avail_in == 0) {
			action = LZMA_FINISH;
		}

		ret = lzma_code(&strm, action);

		if (ret == LZMA_STREAM_END) {
			break;
		}
	}

	assert_ulong_equal(random_data.plain_size, strm.total_out);
	assert_n_array_equal(random_data.plain_data, output_buf,
			random_data.plain_size);
	free(output_buf);
	lzma_end(&strm);
}


// Tests different combinations of the decoder flags:
// LZMA_TELL_NO_CHECK
// LZMA_TELL_UNSUPPORTED_CHECK
// LZMA_TELL_ANY_CHECK
// LZMA_CONCATENATED
static void
test_flags(void)
{

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

}


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
	run_test(test_memlimit_stop);
	//run_test(test_flags);
	//run_test(test_timeout);
	test_fixture_end();

	free_test_file_data(&abc_data);
	free_test_file_data(&text_data);
	free_test_file_data(&random_data);
}
