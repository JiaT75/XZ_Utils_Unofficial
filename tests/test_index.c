///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_index.c
/// \brief      Tests functions handling the lzma_index structure
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "liblzma_tests.h"
#include <stdbool.h>
#include <stdlib.h>

#define MEMLIMIT (LZMA_VLI_C(1) << 20)

#define SMALL_COUNT 3
#define BIG_COUNT 5555

// Allocator that succeeds for the first two allocation but fails the rest.
static void *
my_alloc(void *opaque, size_t a, size_t b)
{
	(void)opaque;

	static unsigned count = 0;
	if (++count > 2)
		return NULL;

	return malloc(a * b);
}

static const lzma_allocator my_allocator = { &my_alloc, NULL, NULL };

static inline bool
coder_loop(lzma_stream *strm, uint8_t *in, size_t in_size,
		uint8_t *out, size_t out_size,
		lzma_ret expected_ret, lzma_action finishing_action)
{
	size_t in_left = in_size;
	size_t out_left = out_size > 0 ? out_size + 1 : 0;
	lzma_action action = LZMA_RUN;
	lzma_ret ret;

	strm->next_in = NULL;
	strm->avail_in = 0;
	strm->next_out = NULL;
	strm->avail_out = 0;

	while (true) {
		if (in_left > 0) {
			if (--in_left == 0)
				action = finishing_action;

			strm->next_in = in++;
			strm->avail_in = 1;
		}

		if (out_left > 0) {
			--out_left;
			strm->next_out = out++;
			strm->avail_out = 1;
		}

		ret = lzma_code(strm, action);
		if (ret != LZMA_OK)
			break;
	}

	bool error = false;

	if (ret != expected_ret)
		error = true;

	if (strm->total_in != in_size || strm->total_out != out_size)
		error = true;

	return error;
}


static inline bool
decoder_loop_ret(lzma_stream *strm, uint8_t *in, size_t in_size,
		lzma_ret expected_ret)
{
	return coder_loop(strm, in, in_size, NULL, 0, expected_ret, LZMA_RUN);
}


static inline bool
decoder_loop(lzma_stream *strm, uint8_t *in, size_t in_size)
{
	return coder_loop(strm, in, in_size, NULL, 0,
			LZMA_STREAM_END, LZMA_RUN);
}

static lzma_index *
create_empty(void)
{
	lzma_index *i = lzma_index_init(NULL);
	assert_true(i != NULL);
	return i;
}


static lzma_index *
create_small(void)
{
	lzma_index *i = lzma_index_init(NULL);
	assert_true(i != NULL);
	assert_int_equal(lzma_index_append(i, NULL, 101, 555), LZMA_OK);
	assert_int_equal(lzma_index_append(i, NULL, 602, 777), LZMA_OK);
	assert_int_equal(lzma_index_append(i, NULL, 804, 999), LZMA_OK);
	return i;
}


static lzma_index *
create_big(void)
{
	lzma_index *i = lzma_index_init(NULL);
	assert_true(i != NULL);

	lzma_vli total_size = 0;
	lzma_vli uncompressed_size = 0;

	// Add pseudo-random sizes (but always the same size values).
	uint32_t n = 11;
	for (size_t j = 0; j < BIG_COUNT; ++j) {
		n = 7019 * n + 7607;
		const uint32_t t = n * 3011;
		assert_int_equal(lzma_index_append(i, NULL, t, n), LZMA_OK);
		total_size += (t + 3) & ~LZMA_VLI_C(3);
		uncompressed_size += n;
	}

	assert_int_equal(lzma_index_block_count(i), BIG_COUNT);
	assert_int_equal(lzma_index_total_size(i), total_size);
	assert_int_equal(lzma_index_uncompressed_size(i), uncompressed_size);
	assert_int_equal(lzma_index_total_size(i) + lzma_index_size(i)
				+ 2 * LZMA_STREAM_HEADER_SIZE,
			lzma_index_stream_size(i));

	return i;
}


static bool
is_equal(const lzma_index *a, const lzma_index *b)
{
	// Compare only the Stream and Block sizes and offsets.
	lzma_index_iter ra, rb;
	lzma_index_iter_init(&ra, a);
	lzma_index_iter_init(&rb, b);

	while (true) {
		bool reta = lzma_index_iter_next(&ra, LZMA_INDEX_ITER_ANY);
		bool retb = lzma_index_iter_next(&rb, LZMA_INDEX_ITER_ANY);
		if (reta)
			return !(reta ^ retb);

		if (ra.stream.number != rb.stream.number
				|| ra.stream.block_count
					!= rb.stream.block_count
				|| ra.stream.compressed_offset
					!= rb.stream.compressed_offset
				|| ra.stream.uncompressed_offset
					!= rb.stream.uncompressed_offset
				|| ra.stream.compressed_size
					!= rb.stream.compressed_size
				|| ra.stream.uncompressed_size
					!= rb.stream.uncompressed_size
				|| ra.stream.padding
					!= rb.stream.padding)
			return false;

		if (ra.stream.block_count == 0)
			continue;

		if (ra.block.number_in_file != rb.block.number_in_file
				|| ra.block.compressed_file_offset
					!= rb.block.compressed_file_offset
				|| ra.block.uncompressed_file_offset
					!= rb.block.uncompressed_file_offset
				|| ra.block.number_in_stream
					!= rb.block.number_in_stream
				|| ra.block.compressed_stream_offset
					!= rb.block.compressed_stream_offset
				|| ra.block.uncompressed_stream_offset
					!= rb.block.uncompressed_stream_offset
				|| ra.block.uncompressed_size
					!= rb.block.uncompressed_size
				|| ra.block.unpadded_size
					!= rb.block.unpadded_size
				|| ra.block.total_size
					!= rb.block.total_size)
			return false;
	}
}


static void
test_equal(void)
{
	lzma_index *a = create_empty();
	lzma_index *b = create_small();
	lzma_index *c = create_big();
	assert_true(a && b && c);

	assert_true(is_equal(a, a));
	assert_true(is_equal(b, b));
	assert_true(is_equal(c, c));

	assert_false(is_equal(a, b));
	assert_false(is_equal(a, c));
	assert_false(is_equal(b, c));

	lzma_index_end(a, NULL);
	lzma_index_end(b, NULL);
	lzma_index_end(c, NULL);
}


static void
test_overflow(void)
{
	// Integer overflow tests
	lzma_index *i = create_empty();

	assert_int_equal(lzma_index_append(i, NULL, LZMA_VLI_MAX - 5, 1234),
			LZMA_DATA_ERROR);

	// TODO

	lzma_index_end(i, NULL);
}


static void
test_copy(const lzma_index *i)
{
	lzma_index *d = lzma_index_dup(i, NULL);
	assert_true(d != NULL);
	assert_true(is_equal(i, d));
	lzma_index_end(d, NULL);
}


static void
test_read(lzma_index *i)
{
	lzma_index_iter r;
	lzma_index_iter_init(&r, i);

	// Try twice so we see that rewinding works.
	for (size_t j = 0; j < 2; ++j) {
		lzma_vli total_size = 0;
		lzma_vli uncompressed_size = 0;
		lzma_vli stream_offset = LZMA_STREAM_HEADER_SIZE;
		lzma_vli uncompressed_offset = 0;
		uint32_t count = 0;

		while (!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)) {
			++count;

			total_size += r.block.total_size;
			uncompressed_size += r.block.uncompressed_size;

			assert_int_equal(r.block.compressed_file_offset,
					stream_offset);
			assert_int_equal(r.block.uncompressed_file_offset,
					uncompressed_offset);

			stream_offset += r.block.total_size;
			uncompressed_offset += r.block.uncompressed_size;
		}

		assert_int_equal(lzma_index_total_size(i), total_size);
		assert_int_equal(lzma_index_uncompressed_size(i), uncompressed_size);
		assert_int_equal(lzma_index_block_count(i), count);

		lzma_index_iter_rewind(&r);
	}
}


static void
test_code(lzma_index *i)
{
	const size_t alloc_size = 128 * 1024;
	uint8_t *buf = malloc(alloc_size);
	assert_true(buf != NULL);

	// Encode
	lzma_stream strm = LZMA_STREAM_INIT;
	assert_int_equal(lzma_index_encoder(&strm, i), LZMA_OK);
	const lzma_vli index_size = lzma_index_size(i);
	assert_false(coder_loop(&strm, NULL, 0, buf, index_size,
			LZMA_STREAM_END, LZMA_RUN));

	// Decode
	lzma_index *d;
	assert_int_equal(lzma_index_decoder(&strm, &d, MEMLIMIT), LZMA_OK);
	assert_true(d == NULL);
	assert_false(decoder_loop(&strm, buf, index_size));

	assert_true(is_equal(i, d));

	lzma_index_end(d, NULL);
	lzma_end(&strm);

	// Decode with hashing
	lzma_index_hash *h = lzma_index_hash_init(NULL, NULL);
	assert_true(h != NULL);
	lzma_index_iter r;
	lzma_index_iter_init(&r, i);
	while (!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK))
		assert_int_equal(lzma_index_hash_append(h, r.block.unpadded_size,
				r.block.uncompressed_size), LZMA_OK);
	size_t pos = 0;
	while (pos < index_size - 1)
		assert_int_equal(lzma_index_hash_decode(h, buf, &pos, pos + 1),
				LZMA_OK);
	assert_int_equal(lzma_index_hash_decode(h, buf, &pos, pos + 1),
			LZMA_STREAM_END);

	lzma_index_hash_end(h, NULL);

	// Encode buffer
	size_t buf_pos = 1;
	assert_int_equal(lzma_index_buffer_encode(i, buf, &buf_pos, index_size),
			LZMA_BUF_ERROR);
	assert_int_equal(buf_pos, 1);

	assert_false(lzma_index_buffer_encode(i, buf, &buf_pos, index_size + 1));
	assert_int_equal(buf_pos, index_size + 1);

	// Decode buffer
	buf_pos = 1;
	uint64_t memlimit = MEMLIMIT;
	assert_int_equal(lzma_index_buffer_decode(&d, &memlimit, NULL, buf, &buf_pos,
			index_size), LZMA_DATA_ERROR);
	assert_int_equal(buf_pos, 1);
	assert_true(d == NULL);

	assert_false(lzma_index_buffer_decode(&d, &memlimit, NULL, buf, &buf_pos,
			index_size + 1));
	assert_int_equal(buf_pos, index_size + 1);
	assert_true(is_equal(i, d));

	lzma_index_end(d, NULL);

	free(buf);
}


static void
test_many(lzma_index *i)
{
	test_copy(i);
	test_read(i);
	test_code(i);
}


static void
test_cat(void)
{
	lzma_index *a, *b, *c;
	lzma_index_iter r;

	// Empty Indexes
	a = create_empty();
	b = create_empty();
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_block_count(a), 0);
	assert_int_equal(lzma_index_stream_size(a), 2 * LZMA_STREAM_HEADER_SIZE + 8);
	assert_int_equal(lzma_index_file_size(a),
			2 * (2 * LZMA_STREAM_HEADER_SIZE + 8));
	lzma_index_iter_init(&r, a);
	assert_true(lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK));

	b = create_empty();
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_block_count(a), 0);
	assert_int_equal(lzma_index_stream_size(a), 2 * LZMA_STREAM_HEADER_SIZE + 8);
	assert_int_equal(lzma_index_file_size(a),
			3 * (2 * LZMA_STREAM_HEADER_SIZE + 8));

	b = create_empty();
	c = create_empty();
	assert_int_equal(lzma_index_stream_padding(b, 4), LZMA_OK);
	assert_int_equal(lzma_index_cat(b, c, NULL), LZMA_OK);
	assert_int_equal(lzma_index_block_count(b), 0);
	assert_int_equal(lzma_index_stream_size(b), 2 * LZMA_STREAM_HEADER_SIZE + 8);
	assert_int_equal(lzma_index_file_size(b),
			2 * (2 * LZMA_STREAM_HEADER_SIZE + 8) + 4);

	assert_int_equal(lzma_index_stream_padding(a, 8), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_block_count(a), 0);
	assert_int_equal(lzma_index_stream_size(a), 2 * LZMA_STREAM_HEADER_SIZE + 8);
	assert_int_equal(lzma_index_file_size(a),
			5 * (2 * LZMA_STREAM_HEADER_SIZE + 8) + 4 + 8);

	assert_true(lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK));
	lzma_index_iter_rewind(&r);
	assert_true(lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK));
	lzma_index_end(a, NULL);

	// Small Indexes
	a = create_small();
	lzma_vli stream_size = lzma_index_stream_size(a);
	lzma_index_iter_init(&r, a);
	for (int i = SMALL_COUNT; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	b = create_small();
	assert_int_equal(lzma_index_stream_padding(a, 4), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_file_size(a), stream_size * 2 + 4);
	assert_true(lzma_index_stream_size(a) > stream_size);
	assert_true(lzma_index_stream_size(a) < stream_size * 2);
	for (int i = SMALL_COUNT; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	lzma_index_iter_rewind(&r);
	for (int i = SMALL_COUNT * 2; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	b = create_small();
	c = create_small();
	assert_int_equal(lzma_index_stream_padding(b, 8), LZMA_OK);
	assert_int_equal(lzma_index_cat(b, c, NULL), LZMA_OK);
	assert_int_equal(lzma_index_stream_padding(a, 12), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_file_size(a), stream_size * 4 + 4 + 8 + 12);

	assert_int_equal(lzma_index_block_count(a), SMALL_COUNT * 4);
	for (int i = SMALL_COUNT * 2; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	lzma_index_iter_rewind(&r);
	for (int i = SMALL_COUNT * 4; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	lzma_index_end(a, NULL);

	// Mix of empty and small
	a = create_empty();
	b = create_small();
	assert_int_equal(lzma_index_stream_padding(a, 4), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	lzma_index_iter_init(&r, a);
	for (int i = SMALL_COUNT; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	lzma_index_end(a, NULL);

	// Big Indexes
	a = create_big();
	stream_size = lzma_index_stream_size(a);
	b = create_big();
	assert_int_equal(lzma_index_stream_padding(a, 4), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_file_size(a), stream_size * 2 + 4);
	assert_true(lzma_index_stream_size(a) > stream_size);
	assert_true(lzma_index_stream_size(a) < stream_size * 2);

	b = create_big();
	c = create_big();
	assert_int_equal(lzma_index_stream_padding(b, 8), LZMA_OK);
	assert_int_equal(lzma_index_cat(b, c, NULL), LZMA_OK);
	assert_int_equal(lzma_index_stream_padding(a, 12), LZMA_OK);
	assert_int_equal(lzma_index_cat(a, b, NULL), LZMA_OK);
	assert_int_equal(lzma_index_file_size(a), stream_size * 4 + 4 + 8 + 12);

	lzma_index_iter_init(&r, a);
	for (int i = BIG_COUNT * 4; i >= 0; --i)
		assert_true(!lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK)
				^ (i == 0));

	lzma_index_end(a, NULL);
}


static void
test_locate(void)
{
	lzma_index *i = lzma_index_init(NULL);
	assert_true(i != NULL);
	lzma_index_iter r;
	lzma_index_iter_init(&r, i);

	// Cannot locate anything from an empty Index.
	assert_true(lzma_index_iter_locate(&r, 0));
	assert_true(lzma_index_iter_locate(&r, 555));

	// One empty Record: nothing is found since there's no uncompressed
	// data.
	assert_int_equal(lzma_index_append(i, NULL, 16, 0), LZMA_OK);
	assert_true(lzma_index_iter_locate(&r, 0));

	// Non-empty Record and we can find something.
	assert_true(lzma_index_append(i, NULL, 32, 5) == LZMA_OK);
	assert_false(lzma_index_iter_locate(&r, 0));
	assert_int_equal(r.block.total_size, 32);
	assert_int_equal(r.block.uncompressed_size, 5);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	// Still cannot find anything past the end.
	assert_true(lzma_index_iter_locate(&r, 5));

	// Add the third Record.
	assert_int_equal(lzma_index_append(i, NULL, 40, 11), LZMA_OK);

	assert_false(lzma_index_iter_locate(&r, 0));
	assert_int_equal(r.block.total_size, 32);
	assert_int_equal(r.block.uncompressed_size, 5);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	assert_false(lzma_index_iter_next(&r, LZMA_INDEX_ITER_BLOCK));
	assert_int_equal(r.block.total_size, 40);
	assert_int_equal(r.block.uncompressed_size, 11);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16 + 32);
	assert_int_equal(r.block.uncompressed_file_offset, 5);

	assert_false(lzma_index_iter_locate(&r, 2));
	assert_int_equal(r.block.total_size, 32);
	assert_int_equal(r.block.uncompressed_size, 5);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	assert_false(lzma_index_iter_locate(&r, 5));
	assert_int_equal(r.block.total_size, 40);
	assert_int_equal(r.block.uncompressed_size, 11);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16 + 32);
	assert_int_equal(r.block.uncompressed_file_offset, 5);

	assert_false(lzma_index_iter_locate(&r, 5 + 11 - 1));
	assert_int_equal(r.block.total_size, 40);
	assert_int_equal(r.block.uncompressed_size, 11);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 16 + 32);
	assert_int_equal(r.block.uncompressed_file_offset, 5);

	assert_true(lzma_index_iter_locate(&r, 5 + 11));
	assert_true(lzma_index_iter_locate(&r, 5 + 15));

	// Large Index
	lzma_index_end(i, NULL);
	i = lzma_index_init(NULL);
	assert_true(i != NULL);
	lzma_index_iter_init(&r, i);

	for (size_t n = 4; n <= 4 * 5555; n += 4)
		assert_int_equal(lzma_index_append(i, NULL, n + 8, n), LZMA_OK);

	assert_int_equal(lzma_index_block_count(i), 5555);

	// First Record
	assert_false(lzma_index_iter_locate(&r, 0));
	assert_int_equal(r.block.total_size, 4 + 8);
	assert_int_equal(r.block.uncompressed_size, 4);
	assert_int_equal(r.block.compressed_file_offset, LZMA_STREAM_HEADER_SIZE);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	assert_false(lzma_index_iter_locate(&r, 3));
	assert_int_equal(r.block.total_size, 4 + 8);
	assert_int_equal(r.block.uncompressed_size, 4);
	assert_int_equal(r.block.compressed_file_offset, LZMA_STREAM_HEADER_SIZE);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	// Second Record
	assert_false(lzma_index_iter_locate(&r, 4));
	assert_int_equal(r.block.total_size, 2 * 4 + 8);
	assert_int_equal(r.block.uncompressed_size, 2 * 4);
	assert_int_equal(r.block.compressed_file_offset,
			LZMA_STREAM_HEADER_SIZE + 4 + 8);
	assert_int_equal(r.block.uncompressed_file_offset, 4);

	// Last Record
	assert_false(lzma_index_iter_locate(
			&r, lzma_index_uncompressed_size(i) - 1));
	assert_int_equal(r.block.total_size, 4 * 5555 + 8);
	assert_int_equal(r.block.uncompressed_size, 4 * 5555);
	assert_int_equal(r.block.compressed_file_offset, lzma_index_total_size(i)
			+ LZMA_STREAM_HEADER_SIZE - 4 * 5555 - 8);
	assert_int_equal(r.block.uncompressed_file_offset,
			lzma_index_uncompressed_size(i) - 4 * 5555);

	// Allocation chunk boundaries. See INDEX_GROUP_SIZE in
	// liblzma/common/index.c.
	const size_t group_multiple = 256 * 4;
	const size_t radius = 8;
	const size_t start = group_multiple - radius;
	lzma_vli ubase = 0;
	lzma_vli tbase = 0;
	size_t n;
	for (n = 1; n < start; ++n) {
		ubase += n * 4;
		tbase += n * 4 + 8;
	}

	while (n < start + 2 * radius) {
		assert_false(lzma_index_iter_locate(&r, ubase + n * 4));

		assert_int_equal(r.block.compressed_file_offset, tbase + n * 4 + 8
				+ LZMA_STREAM_HEADER_SIZE);
		assert_int_equal(r.block.uncompressed_file_offset, ubase + n * 4);

		tbase += n * 4 + 8;
		ubase += n * 4;
		++n;

		assert_int_equal(r.block.total_size, n * 4 + 8);
		assert_int_equal(r.block.uncompressed_size, n * 4);
	}

	// Do it also backwards.
	while (n > start) {
		assert_false(lzma_index_iter_locate(&r, ubase + (n - 1) * 4));

		assert_int_equal(r.block.total_size, n * 4 + 8);
		assert_int_equal(r.block.uncompressed_size, n * 4);

		--n;
		tbase -= n * 4 + 8;
		ubase -= n * 4;

		assert_int_equal(r.block.compressed_file_offset, tbase + n * 4 + 8
				+ LZMA_STREAM_HEADER_SIZE);
		assert_int_equal(r.block.uncompressed_file_offset, ubase + n * 4);
	}

	// Test locating in concatenated Index.
	lzma_index_end(i, NULL);
	i = lzma_index_init(NULL);
	assert_true(i != NULL);
	lzma_index_iter_init(&r, i);
	for (n = 0; n < group_multiple; ++n)
		assert_int_equal(lzma_index_append(i, NULL, 8, 0), LZMA_OK);
	assert_int_equal(lzma_index_append(i, NULL, 16, 1), LZMA_OK);
	assert_false(lzma_index_iter_locate(&r, 0));
	assert_int_equal(r.block.total_size, 16);
	assert_int_equal(r.block.uncompressed_size, 1);
	assert_int_equal(r.block.compressed_file_offset, LZMA_STREAM_HEADER_SIZE + group_multiple * 8);
	assert_int_equal(r.block.uncompressed_file_offset, 0);

	lzma_index_end(i, NULL);
}


static void
test_corrupt(void)
{
	const size_t alloc_size = 128 * 1024;
	uint8_t *buf = malloc(alloc_size);
	assert_true(buf != NULL);
	lzma_stream strm = LZMA_STREAM_INIT;

	lzma_index *i = create_empty();
	assert_int_equal(lzma_index_append(i, NULL, 0, 1), LZMA_PROG_ERROR);
	lzma_index_end(i, NULL);

	// Create a valid Index and corrupt it in different ways.
	i = create_small();
	assert_int_equal(lzma_index_encoder(&strm, i), LZMA_OK);
	assert_false(coder_loop(&strm, NULL, 0, buf, 20,
			LZMA_STREAM_END, LZMA_RUN));
	lzma_index_end(i, NULL);

	// Wrong Index Indicator
	buf[0] ^= 1;
	assert_int_equal(lzma_index_decoder(&strm, &i, MEMLIMIT), LZMA_OK);
	assert_false(decoder_loop_ret(&strm, buf, 1, LZMA_DATA_ERROR));
	buf[0] ^= 1;

	// Wrong Number of Records and thus CRC32 fails.
	--buf[1];
	assert_int_equal(lzma_index_decoder(&strm, &i, MEMLIMIT), LZMA_OK);
	assert_false(decoder_loop_ret(&strm, buf, 10, LZMA_DATA_ERROR));
	++buf[1];

	// Padding not NULs
	buf[15] ^= 1;
	assert_int_equal(lzma_index_decoder(&strm, &i, MEMLIMIT), LZMA_OK);
	assert_false(decoder_loop_ret(&strm, buf, 16, LZMA_DATA_ERROR));

	lzma_end(&strm);
	free(buf);
}

static void
test_empty_index(void)
{
	lzma_index *i = create_empty();
	test_many(i);
	lzma_index_end(i, NULL);
}

static void
test_small_index(void)
{
	lzma_index *i = create_small();
	test_many(i);
	lzma_index_end(i, NULL);
}

static void
test_big_index(void)
{
	lzma_index *i = create_big();
	test_many(i);
	lzma_index_end(i, NULL);
}

static void
test_lzma_index_dup_empty_stream(void)
{
	// Test for the bug fix 21515d79d778b8730a434f151b07202d52a04611:
	// liblzma: Fix lzma_index_dup() for empty Streams.
	lzma_index *i = create_empty();
	assert_int_equal(lzma_index_stream_padding(i, 4), LZMA_OK);
	test_copy(i);
	lzma_index_end(i, NULL);
}

static void
test_memory_leak_error_path_lzma_index_dup(void)
{
	// Test for the bug fix 3bf857edfef51374f6f3fffae3d817f57d3264a0:
	// liblzma: Fix a memory leak in error path of lzma_index_dup().
	// Use Valgrind to see that there are no leaks.
	lzma_index *i = create_small();
	assert_true(lzma_index_dup(i, &my_allocator) == NULL);
	lzma_index_end(i, NULL);
}

void
test_lzma_index_structure(void){
	test_fixture_start();
	run_test(test_equal);
	run_test(test_overflow);
	run_test(test_empty_index);
	run_test(test_small_index);
	run_test(test_big_index);
	run_test(test_cat);
	run_test(test_locate);
	run_test(test_corrupt);
	run_test(test_lzma_index_dup_empty_stream);
	run_test(test_memory_leak_error_path_lzma_index_dup);
	test_fixture_end();
}
