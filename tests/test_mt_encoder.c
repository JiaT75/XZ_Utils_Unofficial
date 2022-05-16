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
#include <stdlib.h>

// Very small block size to help with testing
#define BLOCK_SIZE 0x1000

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

static void
test_basic_mt_encode(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
		.preset = 6,
		.filters = NULL,
		.check = LZMA_CHECK_CRC64
	};

	assert_int_equal(LZMA_OK, lzma_stream_encoder_mt(&strm, &options));

	// Safe upperbound out limit is input size * 2 since the
	// overhead of the small blocks and compression reduction
	// should not be worse than 2x the size of the original
	size_t upperbound_out_limit = text_data.compressed_size * 2;
	uint8_t *out_buf = malloc(upperbound_out_limit);

	strm.avail_in = text_data.compressed_size;
	strm.avail_out = upperbound_out_limit;
	strm.next_in = text_data.plain_data;
	strm.next_out = out_buf;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	// TODO verify output

	free(out_buf);
	lzma_end(&strm);
}


static void
verify_sizes_unset_block_header(const uint8_t *out_buf, lzma_block *block)
{
	uint32_t header_size = lzma_block_header_size_decode(out_buf[0]);
	block->header_size = header_size;
	assert_int_equal(LZMA_OK, lzma_block_header_decode(block, NULL,
			out_buf));
	assert_ulong_equal(LZMA_VLI_UNKNOWN, block->compressed_size);
	assert_ulong_equal(LZMA_VLI_UNKNOWN, block->uncompressed_size);
}


static void
verify_sizes_set_block_header(const uint8_t *out_buf, lzma_block *block)
{
	uint32_t header_size = lzma_block_header_size_decode(out_buf[0]);
	block->header_size = header_size;
	assert_int_equal(LZMA_OK, lzma_block_header_decode(block, NULL,
			out_buf));
	assert_true(block->compressed_size != LZMA_VLI_UNKNOWN);
	assert_true(block->uncompressed_size != LZMA_VLI_UNKNOWN);
}


static bool
is_block_compressed(uint8_t *block)
{
	return !(block[0] == 1 || block[0] == 2);
}


static void
test_sync_flush(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
		.preset = 6,
		.filters = NULL,
		.check = LZMA_CHECK_CRC64
	};

	assert_int_equal(LZMA_OK, lzma_stream_encoder_mt(&strm, &options));

	size_t upperbound_out_limit = text_data.compressed_size * 2;
	uint8_t *out_buf = malloc(upperbound_out_limit);

	// Encode 4 blocks
	strm.avail_in = BLOCK_SIZE * 4;
	strm.avail_out = upperbound_out_limit;
	strm.next_in = text_data.plain_data;
	strm.next_out = out_buf;

	assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));

	// No more input should be available
	assert_int_equal(0, strm.avail_in);

	// Encode first half of the next block
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	// No more input should be available
	assert_int_equal(0, strm.avail_in);

	// First, skip past stream header
	uint8_t *out_pos = out_buf + LZMA_STREAM_HEADER_SIZE;
	lzma_filter block_filters[LZMA_FILTERS_MAX + 1];
	lzma_block block = {
		.version = 1,
		.filters = block_filters,
		.check = LZMA_CHECK_CRC64
	};

	// Skip past block header, compressed data, block padding, check
	for (int i = 0; i < 4; i++) {
		uint32_t header_size = lzma_block_header_size_decode(
				out_pos[0]);
		block.header_size = header_size;
		assert_int_equal(LZMA_OK, lzma_block_header_decode(&block,
				NULL, out_pos));
		assert_true(block.compressed_size < LZMA_VLI_MAX);
		uint32_t padding_size = block.compressed_size & 3 ?
				4 - block.compressed_size % 4 : 0;
		out_pos += block.compressed_size + header_size +
				padding_size +
				lzma_check_size(block.check);
	}

	// Make sure last block does not have compressed and uncompressed
	// sizes encoded
	verify_sizes_unset_block_header(out_pos, &block);

	// Encode second half of the block
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));

	assert_true(strm.next_out > out_pos);
	out_pos = strm.next_out;
	// Encode 3/4 of the next block
	strm.avail_in = (BLOCK_SIZE * 3) / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	// Make sure last block does not have sizes in header
	verify_sizes_unset_block_header(out_pos, &block);

	// Encode last 1/4 of block
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));

	assert_true(strm.next_out > out_pos);
	out_pos = strm.next_out;
	// Encode full block
	strm.avail_in = BLOCK_SIZE;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	// Last block should have sizes in header
	verify_sizes_set_block_header(out_pos, &block);

	assert_true(strm.next_out > out_pos);
	out_pos = strm.next_out;

	// Encode 1.5 blocks
	strm.avail_in = BLOCK_SIZE + BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	// First block should have sizes in header
	verify_sizes_set_block_header(out_pos, &block);

	// Last block should NOT have sizes in header
	uint32_t header_size = lzma_block_header_size_decode(out_pos[0]);
	uint32_t padding_size = block.compressed_size & 3 ?
				4 - block.compressed_size % 4 : 0;
	out_pos += block.compressed_size + header_size +
			padding_size +
			lzma_check_size(block.check);
	verify_sizes_unset_block_header(out_pos, &block);

	// Test multiple sync flushes in the same block
	assert_true(strm.next_out > out_pos);
	out_pos = strm.next_out;
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END,
			lzma_code(&strm, LZMA_SYNC_FLUSH));
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	// Next decode stream
	lzma_stream decode_strm = LZMA_STREAM_INIT;
	assert_int_equal(LZMA_OK, lzma_stream_decoder(&decode_strm,
			UINT64_MAX, 0));

	uint8_t *decode_buf = malloc(strm.total_in);
	assert_true(decode_buf != 0);

	decode_strm.avail_in = strm.total_out;
	decode_strm.avail_out = strm.total_in;
	decode_strm.next_in = out_buf;
	decode_strm.next_out = decode_buf;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&decode_strm,
			LZMA_FINISH));

	assert_int_equal(0, decode_strm.avail_in);
	assert_int_equal(0, decode_strm.avail_out);

	assert_n_array_equal(text_data.plain_data, decode_buf,
			decode_strm.total_out);

	free(out_buf);
	free(decode_buf);
	lzma_end(&strm);
	lzma_end(&decode_strm);
}


static void
test_sync_flush_uncompressable(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
		.preset = 6,
		.filters = NULL,
		.check = LZMA_CHECK_CRC64
	};

	assert_int_equal(LZMA_OK, lzma_stream_encoder_mt(&strm, &options));

	size_t upperbound_out_limit = text_data.compressed_size * 2;
	uint8_t *out_buf = malloc(upperbound_out_limit);

	// First encode 2 blocks normally
	strm.avail_in = BLOCK_SIZE * 2;
	strm.avail_out = upperbound_out_limit;
	strm.next_in = random_data.plain_data;
	strm.next_out = out_buf;

	assert_int_equal(LZMA_OK, lzma_code(&strm, LZMA_RUN));

	assert_int_equal(0, strm.avail_in);

	// Test uncompressable partial blocks with sync flush
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));
	assert_int_equal(0, strm.avail_in);

	// Verify sizes in header are not set
	// First, skip past stream header
	uint8_t *out_pos = out_buf + LZMA_STREAM_HEADER_SIZE;
	lzma_filter block_filters[LZMA_FILTERS_MAX + 1];
	lzma_block block = {
		.version = 1,
		.filters = block_filters,
		.check = LZMA_CHECK_CRC64
	};

	// Skip past block header, compressed data, block padding, check
	for (int i = 0; i < 2; i++) {
		uint32_t header_size = lzma_block_header_size_decode(
				out_pos[0]);
		block.header_size = header_size;
		assert_int_equal(LZMA_OK, lzma_block_header_decode(&block,
				NULL, out_pos));
		assert_true(block.compressed_size < LZMA_VLI_MAX);
		uint32_t padding_size = block.compressed_size & 3 ?
				4 - block.compressed_size % 4 : 0;
		out_pos += block.compressed_size + header_size +
				padding_size +
				lzma_check_size(block.check);
	}

	verify_sizes_unset_block_header(out_pos, &block);
	assert_false(is_block_compressed(out_pos +
			lzma_block_header_size_decode(out_pos[0])));

	// Encode second half of the block
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));
	assert_int_equal(0, strm.avail_in);
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;

	// Test uncompressable full block with sync flush
	strm.avail_in = BLOCK_SIZE;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));
	assert_int_equal(0, strm.avail_in);
	// Last block should have sizes in header
	verify_sizes_set_block_header(out_pos, &block);

	// Test compressable first half, sync flush, uncompressable
	// second half
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;
	const uint8_t *position_in_random = strm.next_in;
	strm.next_in = text_data.plain_data;
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));

	verify_sizes_unset_block_header(out_pos, &block);
	assert_true(is_block_compressed(out_pos +
			lzma_block_header_size_decode(out_pos[0])));
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;

	strm.next_in = position_in_random;
	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));

	assert_false(is_block_compressed(out_pos));
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;

	strm.avail_in = BLOCK_SIZE / 4;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));

	assert_false(is_block_compressed(out_pos));

	// Test uncompressable first half, sync flush, compressable
	// second half
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));

	verify_sizes_unset_block_header(out_pos, &block);
	assert_false(is_block_compressed(out_pos +
			lzma_block_header_size_decode(out_pos[0])));
	assert_true(strm.next_out > out_buf);
	out_pos = strm.next_out;

	strm.next_in = text_data.plain_data + BLOCK_SIZE / 2;
	strm.avail_in = BLOCK_SIZE / 2;
	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_SYNC_FLUSH));

	assert_true(is_block_compressed(out_pos));

	assert_int_equal(LZMA_STREAM_END, lzma_code(&strm, LZMA_FINISH));

	lzma_stream decode_strm = LZMA_STREAM_INIT;
	assert_int_equal(LZMA_OK, lzma_stream_decoder(&decode_strm,
			UINT64_MAX, 0));

	uint8_t *decode_buf = malloc(strm.total_in);
	assert_true(decode_buf != 0);

	decode_strm.avail_in = strm.total_out;
	decode_strm.avail_out = strm.total_in;
	decode_strm.next_in = out_buf;
	decode_strm.next_out = decode_buf;

	assert_int_equal(LZMA_STREAM_END, lzma_code(&decode_strm,
			LZMA_FINISH));

	assert_int_equal(0, decode_strm.avail_in);
	assert_int_equal(0, decode_strm.avail_out);

	// The first 4 blocks should be from the random data
	assert_n_array_equal(random_data.plain_data, decode_buf,
			BLOCK_SIZE * 4);

	// The next half block should be from the text data
	assert_n_array_equal(text_data.plain_data, (decode_buf +
			(BLOCK_SIZE * 4)), BLOCK_SIZE / 2);
	// The next block should be from the random data
	assert_n_array_equal((random_data.plain_data + (BLOCK_SIZE * 4)),
			(decode_buf + (BLOCK_SIZE * 4) + BLOCK_SIZE / 2),
			BLOCK_SIZE);
	// The last half block should be from the text data
	assert_n_array_equal((text_data.plain_data + (BLOCK_SIZE / 2)),
			(decode_buf + (BLOCK_SIZE * 5) + BLOCK_SIZE / 2),
			BLOCK_SIZE / 2);

	free(out_buf);
	free(decode_buf);
	lzma_end(&strm);
	lzma_end(&decode_strm);
}


static void
test_sync_flush_buf_error(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_mt options = {
		.flags = 0,
		.threads = 4,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
		.preset = 6,
		.filters = NULL,
		.check = LZMA_CHECK_CRC64
	};

	assert_int_equal(LZMA_OK, lzma_stream_encoder_mt(&strm, &options));

	static uint32_t out_size = BLOCK_SIZE * 4;
	uint8_t *out_buf = malloc(out_size);
	assert_true(out_buf != NULL);

	strm.next_in = text_data.plain_data;
	strm.avail_out = out_size;
	strm.next_out = out_buf;

	lzma_ret ret = LZMA_OK;
	for (int i = 0; i < BLOCK_SIZE; i++) {
		strm.avail_in = 1;
		ret = lzma_code(&strm, LZMA_SYNC_FLUSH);
		if (ret != LZMA_STREAM_END)
			break;
	}

	assert_int_equal(LZMA_PROG_ERROR, ret);

	free(out_buf);
	lzma_end(&strm);
}


static void
test_lzma_stream_encoder_mt_memusage(void)
{
	// TODO
}


void
test_mt_encoder(void)
{
	assert_true(prepare_test_file_data(&abc_data));
	assert_true(prepare_test_file_data(&text_data));
	assert_true(prepare_test_file_data(&random_data));

	test_fixture_start();
	run_test(test_basic_mt_encode);
	run_test(test_sync_flush);
	run_test(test_sync_flush_uncompressable);
	run_test(test_sync_flush_buf_error);
	run_test(test_lzma_stream_encoder_mt_memusage);
	test_fixture_end();

	free_test_file_data(&abc_data);
	free_test_file_data(&text_data);
	free_test_file_data(&random_data);
}
