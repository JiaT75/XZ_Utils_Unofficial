///////////////////////////////////////////////////////////////////////////////
//
/// \file       stream_decoder_mt.c
/// \brief      Multithreaded .xz Stream decoder
//
//  Authors:    Sebastian Andrzej Siewior
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "block_decoder.h"
#include "stream_decoder.h"
#include "index.h"
#include "outqueue.h"


typedef enum {
	/// Waiting for work.
	/// Main thread may change this to THR_RUN or THR_EXIT.
	THR_IDLE,

	/// Decoding is in progress.
	/// Main thread may change this to THR_STOP or THR_EXIT.
	/// The worker thread may change this to THR_IDLE.
	THR_RUN,

	/// The main thread wants the thread to stop whatever it was doing
	/// but not exit. Main thread may change this to THR_EXIT.
	/// The worker thread may change this to THR_IDLE.
	THR_STOP,

	/// The main thread wants the thread to exit.
	THR_EXIT,

} worker_state;


struct worker_thread {
	/// Worker state is protected with our mutex.
	worker_state state;

	/// Input buffer that will contain the whole Block except Block Header.
	uint8_t *in;

	/// Amount of memory allocated for "in"
	size_t in_size;

	/// Number of bytes written to "in" by the main thread
	size_t in_filled;

	/// Number of bytes consumed from "in" by the worker thread.
	size_t in_pos;

	/// Amount of uncompressed data that has been decoded. This local
	/// copy is needed because updating outbuf->pos requires locking
	/// the main mutex (coder->mutex).
	size_t out_pos;

	/// Pointer to the main structure is needed to (1) lock the main
	/// mutex (coder->mutex) when updating outbuf->pos and (2) when
	/// putting this thread back to the stack of free threads.
	struct lzma_stream_coder *coder;

	/// The allocator is set by the main thread. Since a copy of the
	/// pointer is kept here, the application must not change the
	/// allocator before calling lzma_end().
	const lzma_allocator *allocator;

	/// Output queue buffer to which the uncompressed data is written.
	lzma_outbuf *outbuf;

	/// Amount of compressed data that has already been decompressed.
	/// This is updated from in_pos when our mutex is locked.
	/// This is size_t, not uint64_t, because per-thread progress
	/// is limited to sizes of allocated buffers.
	size_t progress_in;

	/// Like progress_in but for uncompressed data.
	size_t progress_out;

	/// Updating outbuf->pos requires locking the main mutex
	/// (coder->mutex). Since the main thread will only read output
	/// from the oldest outbuf in the queue, only the worker thread
	/// that is associated with the oldest outbuf needs to update its
	/// outbuf->pos. This avoids useless mutex contention that would
	/// happen if all worker threads were frequently locking the main
	/// mutex to update their outbuf->pos.
	///
	/// Only when partial_update is true, this worker thread will update
	/// outbuf->pos after each call to the Block decoder.
	bool partial_update;

	/// Block decoder
	lzma_next_coder block_decoder;

	/// Thread-specific Block options are needed because the Block
	/// decoder modifies the struct given to it at initialization.
	lzma_block block_options;

	/// Filter chain memory usage
	uint64_t mem_filters;

	/// Next structure in the stack of free worker threads.
	struct worker_thread *next;

	mythread_mutex mutex;
	mythread_cond cond;

	/// The ID of this thread is used to join the thread
	/// when it's not needed anymore.
	mythread thread_id;
};


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


/// Enables updating of outbuf->pos. This is a callback function that is
/// used with lzma_outq_enable_partial_output().
static void
worker_enable_partial_update(void *thr_ptr)
{
	struct worker_thread *thr = thr_ptr;

	mythread_sync(thr->mutex) {
		thr->partial_update = true;
		// Signal to worker thread to wake it up
		// in case it has a partial update ready
		mythread_cond_signal(&thr->cond);
	}
}


/// Things do to at THR_STOP or when finishing a Block.
/// This is called with thr->mutex locked.
static void
worker_stop(struct worker_thread *thr)
{
	// Update memory usage counters.
	thr->coder->mem_in_use -= thr->in_size;
	thr->in_size = 0; // thr->in was freed above.

	thr->coder->mem_in_use -= thr->mem_filters;
	thr->coder->mem_cached += thr->mem_filters;

	// Put this thread to the stack of free threads.
	thr->next = thr->coder->threads_free;
	thr->coder->threads_free = thr;

	mythread_cond_signal(&thr->coder->cond);
	return;
}


static MYTHREAD_RET_TYPE
worker_decoder(void *thr_ptr)
{
	struct worker_thread *thr = thr_ptr;
	size_t in_filled;
	lzma_ret ret;

next_loop_lock:

	mythread_mutex_lock(&thr->mutex);
next_loop_unlocked:

	if (thr->state == THR_IDLE) {
		mythread_cond_wait(&thr->cond, &thr->mutex);
		goto next_loop_unlocked;
	}

	if (thr->state == THR_EXIT) {
		mythread_mutex_unlock(&thr->mutex);

		lzma_free(thr->in, thr->allocator);
		lzma_next_end(&thr->block_decoder, thr->allocator);

		mythread_mutex_destroy(&thr->mutex);
		mythread_cond_destroy(&thr->cond);

		return MYTHREAD_RET_VALUE;
	}

	if (thr->state == THR_STOP) {
		thr->state = THR_IDLE;
		mythread_mutex_unlock(&thr->mutex);

		mythread_sync(thr->coder->mutex) {
			worker_stop(thr);
		}

		goto next_loop_lock;
	}

	assert(thr->state == THR_RUN);

	in_filled = thr->in_filled;

	if (in_filled == thr->in_pos) {
		mythread_cond_wait(&thr->cond, &thr->mutex);
		// If thr->partial_update is true and we have no new update,
		// tell the main thread the progress made to avoid a
		// race condition with the main thread setting partial
		// update and this thread sleeping until more input
		// arrives. This is only necessary if there is a truncated
		// file
		if (thr->partial_update && in_filled == thr->in_pos) {
			mythread_sync(thr->coder->mutex) {
				thr->outbuf->pos = thr->out_pos;
				thr->outbuf->decoder_in_pos = thr->in_pos;
				mythread_cond_signal(&thr->coder->cond);
			}
		}
		goto next_loop_unlocked;
	}

	mythread_mutex_unlock(&thr->mutex);

	// Pass the input in small chunks to the Block decoder.
	// This way we react reasonably fast if we are told to stop/exit,
	// and (when partial_update is true) we tell about our progress
	// to the main thread frequently enough.
	const size_t chunk_size = 16384;
	if ((in_filled - thr->in_pos) > chunk_size)
		in_filled = thr->in_pos + chunk_size;

	ret = thr->block_decoder.code(
			thr->block_decoder.coder, thr->allocator,
			thr->in, &thr->in_pos, in_filled,
			thr->outbuf->buf, &thr->out_pos,
			thr->outbuf->allocated, LZMA_RUN);

	if (ret == LZMA_OK) {
		bool partial_update;

		mythread_sync(thr->mutex) {
			// Update progress info for get_progress().
			thr->progress_in = thr->in_pos;
			thr->progress_out = thr->out_pos;

			partial_update = thr->partial_update;
		}

		if (partial_update) {
			// The main thread is reading decompressed data
			// from thr->outbuf. Tell the main thread about
			// our progress.
			//
			// NOTE: It's possible that we consumed input without
			// producing any new output so it's possible that
			// only in_pos has changed.
			mythread_sync(thr->coder->mutex) {
				thr->outbuf->pos = thr->out_pos;
				thr->outbuf->decoder_in_pos = thr->in_pos;
				mythread_cond_signal(&thr->coder->cond);
			}
		}

		goto next_loop_lock;
	}

	// Either we finished successfully (LZMA_STREAM_END) or an error
	// occurred. Both cases are handled almost identically. The error
	// case requires updating thr->coder->thread_error.
	//
	// The sizes are in the Block Header and the Block decoder
	// checks that they match, thus we know these:
	assert(ret != LZMA_STREAM_END || thr->in_pos == thr->in_size);
	assert(ret != LZMA_STREAM_END
		|| thr->out_pos == thr->block_options.uncompressed_size);

	// Free the input buffer. Don't update in_size as we need
	// it later to update thr->coder->mem_in_use.
	lzma_free(thr->in, thr->allocator);
	thr->in = NULL;

	mythread_sync(thr->mutex) {
		if (thr->state != THR_EXIT)
			thr->state = THR_IDLE;
	}

	mythread_sync(thr->coder->mutex) {
		// Move our progress info to the main thread.
		thr->coder->progress_in += thr->in_pos;
		thr->coder->progress_out += thr->out_pos;
		thr->progress_in = 0;
		thr->progress_out = 0;

		// Mark the outbuf as finished.
		thr->outbuf->pos = thr->out_pos;
		thr->outbuf->decoder_in_pos = thr->in_pos;
		thr->outbuf->finished = true;
		thr->outbuf->finish_ret = ret;
		thr->outbuf = NULL;

		// If an error occurred, tell it to the main thread.
		if (ret != LZMA_STREAM_END
				&& thr->coder->thread_error == LZMA_OK)
			thr->coder->thread_error = ret;

		worker_stop(thr);
	}

	goto next_loop_lock;
}


/// Tells the worker threads to exit and waits for them to terminate.
static void
threads_end(struct lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		mythread_sync(coder->threads[i].mutex) {
			coder->threads[i].state = THR_EXIT;
			mythread_cond_signal(&coder->threads[i].cond);
		}
	}

	for (uint32_t i = 0; i < coder->threads_initialized; ++i)
		mythread_join(coder->threads[i].thread_id);

	lzma_free(coder->threads, allocator);
	coder->threads_initialized = 0;
	coder->threads = NULL;
	coder->threads_free = NULL;

	// The threads don't update these when they exit. Do it here.
	coder->mem_in_use = 0;
	coder->mem_cached = 0;

	return;
}


static void
threads_stop(struct lzma_stream_coder *coder)
{
	for (uint32_t i = 0; i < coder->threads_initialized; ++i) {
		mythread_sync(coder->threads[i].mutex) {
			// The state must be changed conditionally because
			// THR_IDLE -> THR_STOP is not a valid state change.
			if (coder->threads[i].state != THR_IDLE) {
				coder->threads[i].state = THR_STOP;
				mythread_cond_signal(&coder->threads[i].cond);
			}
		}
	}

	return;
}


/// Initialize a new worker_thread structure and create a new thread.
static lzma_ret
initialize_new_thread(struct lzma_stream_coder *coder,
		const lzma_allocator *allocator)
{
	// Allocate the coder->threads array if needed. It's done here instead
	// of when initializing the decoder because we don't need this if we
	// use the direct mode (we may even free coder->threads in the middle
	// of the file if we switch from threaded to direct mode).
	if (coder->threads == NULL) {
		coder->threads = lzma_alloc(
			coder->threads_max * sizeof(struct worker_thread),
			allocator);

		if (coder->threads == NULL)
			return LZMA_MEM_ERROR;
	}

	// Pick a free structure.
	assert(coder->threads_initialized < coder->threads_max);
	struct worker_thread *thr
			= &coder->threads[coder->threads_initialized];

	if (mythread_mutex_init(&thr->mutex))
		goto error_mutex;

	if (mythread_cond_init(&thr->cond))
		goto error_cond;

	thr->state = THR_IDLE;
	thr->in = NULL;
	thr->in_size = 0;
	thr->allocator = allocator;
	thr->coder = coder;
	thr->outbuf = NULL;
	thr->block_decoder = LZMA_NEXT_CODER_INIT;
	thr->mem_filters = 0;

	if (mythread_create(&thr->thread_id, worker_decoder, thr))
		goto error_thread;

	++coder->threads_initialized;
	coder->thr = thr;

	return LZMA_OK;

error_thread:
	mythread_cond_destroy(&thr->cond);

error_cond:
	mythread_mutex_destroy(&thr->mutex);

error_mutex:
	return LZMA_MEM_ERROR;
}


static lzma_ret
get_thread(struct lzma_stream_coder *coder, const lzma_allocator *allocator)
{
	// If there is a free structure on the stack, use it.
	mythread_sync(coder->mutex) {
		if (coder->threads_free != NULL) {
			coder->thr = coder->threads_free;
			coder->threads_free = coder->threads_free->next;

			// The thread is no longer in the cache so substract
			// it from the cached memory usage. Don't add it
			// to mem_in_use though; the caller will handle it
			// since it knows how much memory it will actually
			// use (the filter chain might change).
			coder->mem_cached -= coder->thr->mem_filters;
		}
	}

	if (coder->thr == NULL) {
		assert(coder->threads_initialized < coder->threads_max);

		// Initialize a new thread.
		return_if_error(initialize_new_thread(coder, allocator));
	}

	coder->thr->in_filled = 0;
	coder->thr->in_pos = 0;
	coder->thr->out_pos = 0;

	coder->thr->progress_in = 0;
	coder->thr->progress_out = 0;

	coder->thr->partial_update = false;

	return LZMA_OK;
}


static lzma_ret
read_output_and_wait(struct lzma_stream_coder *coder,
		const lzma_allocator *allocator,
		uint8_t *restrict out, size_t *restrict out_pos,
		size_t out_size,
		bool *input_is_possible,
		bool waiting_allowed,
		mythread_condtime *wait_abs, bool *has_blocked)
{
	lzma_ret ret = LZMA_OK;

	mythread_sync(coder->mutex) {
		do {
			// Get as much output from the queue as is possible
			// without blocking.
			do {
				ret = lzma_outq_read(&coder->outq, allocator,
						out, out_pos, out_size,
						NULL, NULL);

				// If a Block was finished, tell the worker
				// thread of the next Block (if it is still
				// running) to start telling the main thread
				// when new output is available.
				if (ret == LZMA_STREAM_END)
					lzma_outq_enable_partial_output(
						&coder->outq,
						&worker_enable_partial_update);

				// Loop until a Block wasn't finished.
				// It's important to loop around even if
				// *out_pos == out_size because there could
				// be an empty Block that will return
				// LZMA_STREAM_END without needing any
				// output space.
			} while (ret == LZMA_STREAM_END);

			// Check if lzma_outq_read reported an error from
			// the Block decoder.
			if (ret != LZMA_OK)
				break;

			// Check if any thread has indicated an error.
			if (coder->thread_error != LZMA_OK) {
				if (coder->pending_error == LZMA_OK)
					coder->pending_error
							= coder->thread_error;

				// FIXME? Add a flag to do this conditionally?
				// That way errors would get reported to the
				// application without a delay.
// 				if (coder->fast_errors) {
// 					ret = coder->thread_error;
// 					break;
// 				}
			}

			// Check if decoding of the next Block can be started.
			// The memusage of the active threads must be low
			// enough, there must be a free buffer slot in the
			// output queue, and there must be a free thread
			// (that can be either created or an existing one
			// reused).
			//
			// NOTE: This is checked after reading the output
			// above because reading the output can free a slot in
			// the output queue and also reduce active memusage.
			//
			// NOTE: If output queue is empty, then input will
			// always be possible.
			if (input_is_possible != NULL
					&& coder->memlimit_threading
						- coder->mem_in_use
						- coder->outq.mem_in_use
						>= coder->mem_next_block
					&& lzma_outq_has_buf(&coder->outq)
					&& (coder->threads_initialized
							< coder->threads_max
						|| coder->threads_free
							!= NULL)) {
				*input_is_possible = true;
				break;
			}

			// If the caller doesn't want us to block, return now.
			if (!waiting_allowed)
				break;

			// This check is needed only when input_is_possible
			// is NULL. We must return if we aren't waiting for
			// input to become possible and there is no more
			// output coming from the queue.
			if (lzma_outq_is_empty(&coder->outq)) {
				assert(input_is_possible == NULL);
				break;
			}

			// If there is more data available from the queue,
			// our out buffer must be full and we need to return
			// so that the application can provide more output
			// space.
			//
			// NOTE: In general lzma_outq_is_readable() can return
			// true also when there are no more bytes available.
			// This can happen when a Block has finished without
			// providing any new output. We know that this is not
			// the case because in the beginning of this loop we
			// tried to read as much as possible even when we had
			// no output space left and the mutex has been locked
			// all the time (so worker threads cannot have changed
			// anything). Thus there must be actual pending output
			// in the queue.
			if (lzma_outq_is_readable(&coder->outq)) {
				assert(*out_pos == out_size);
				break;
			}

			// If the application stops providing more input
			// in the middle of a Block, there will eventually
			// be one worker thread left that is stuck waiting for
			// more input (that might never arrive) and a matching
			// outbuf which the worker thread cannot finish due
			// to lack of input. We must detect this situation,
			// otherwise we would end up waiting indefinitely
			// (if no timeout is in use) or keep returning
			// LZMA_TIMED_OUT while making no progress. Thus, the
			// application would never get LZMA_BUF_ERROR from
			// lzma_code() which would tell the application that
			// no more progress is possible. No LZMA_BUF_ERROR
			// means that, for example, truncated .xz files could
			// cause an infinite loop.
			//
			// A worker thread doing partial updates will
			// store not only the output position in outbuf->pos
			// but also the matching input position in
			// outbuf->decoder_in_pos. Here we check if that
			// input position matches the amount of input that
			// the worker thread has been given (in_filled).
			// If so, we must return and not wait as no more
			// output will be coming without first getting more
			// input to the worker thread. If the application
			// keeps calling lzma_code() without providing more
			// input, it will eventually get LZMA_BUF_ERROR.
			//
			// NOTE: We can read partial_update and in_filled
			// without thr->mutex as only the main thread
			// modifies these variables. decoder_in_pos requires
			// coder->mutex which we are already holding.
			if (coder->thr != NULL && coder->thr->partial_update) {
				// There is exactly one outbuf in the queue.
				assert(coder->thr->outbuf == coder->outq.head);
				assert(coder->thr->outbuf == coder->outq.tail);

				if (coder->thr->outbuf->decoder_in_pos
						== coder->thr->in_filled)
					break;
			}

			// Wait for input or output to become possible.
			if (coder->timeout != 0) {
				// See the comment in stream_encoder_mt.c
				// about why mythread_condtime_set() is used
				// like this.
				//
				// FIXME?
				// In contrast to the encoder, this calls
				// _condtime_set while the mutex is locked.
				if (!*has_blocked) {
					*has_blocked = true;
					mythread_condtime_set(wait_abs,
							&coder->cond,
							coder->timeout);
				}

				if (mythread_cond_timedwait(&coder->cond,
						&coder->mutex,
						wait_abs) != 0) {
					ret = LZMA_TIMED_OUT;
					break;
				}
			} else {
				mythread_cond_wait(&coder->cond,
						&coder->mutex);
			}
		} while (ret == LZMA_OK);
	}

	// If we are returning an error, then the application cannot get
	// more output from us and thus keeping the threads running is
	// useless and waste of CPU time.
	if (ret != LZMA_OK && ret != LZMA_TIMED_OUT)
		threads_stop(coder);

	return ret;
}


static lzma_ret
decode_block_header(struct lzma_stream_coder *coder,
		const lzma_allocator *allocator, const uint8_t *restrict in,
		size_t *restrict in_pos, size_t in_size)
{
	if (*in_pos >= in_size)
		return LZMA_OK;

	if (coder->pos == 0) {
		// Detect if it's Index.
		if (in[*in_pos] == 0x00)
			return LZMA_INDEX_DETECTED;

		// Calculate the size of the Block Header. Note that
		// Block Header decoder wants to see this byte too
		// so don't advance *in_pos.
		coder->block_options.header_size
				= lzma_block_header_size_decode(
					in[*in_pos]);
	}

	// Copy the Block Header to the internal buffer.
	lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
			coder->block_options.header_size);

	// Return if we didn't get the whole Block Header yet.
	if (coder->pos < coder->block_options.header_size)
		return LZMA_OK;

	coder->pos = 0;

	// Version 1 is needed to support the .ignore_check option.
	coder->block_options.version = 1;

	// Block Header decoder will initialize all members of this array
	// so we don't need to do it here.
	coder->block_options.filters = coder->filters;

	// Decode the Block Header.
	return_if_error(lzma_block_header_decode(&coder->block_options,
			allocator, coder->buffer));

	// If LZMA_IGNORE_CHECK was used, this flag needs to be set.
	// It has to be set after lzma_block_header_decode() because
	// it always resets this to false.
	coder->block_options.ignore_check = coder->ignore_check;

	// coder->block_options is ready now.
	return LZMA_STREAM_END;
}


static void
cleanup_filters(lzma_filter *filters, const lzma_allocator *allocator)
{
	for (uint32_t i = 0; i < LZMA_FILTERS_MAX; ++i) {
		lzma_free(filters[i].options, allocator);
		filters[i].options = NULL;
	}

	return;
}


/// Get the size of the Compressed Data + Block Padding + Check.
static size_t
comp_blk_size(const struct lzma_stream_coder *coder)
{
	return vli_ceil4(coder->block_options.compressed_size)
			+ lzma_check_size(coder->stream_flags.check);
}


/// Returns true if the size (compressed or uncompressed) is such that
/// threaded decompression cannot be used. Sizes that are too big compared
/// to SIZE_MAX must be rejected to avoid integer overflows and truncations
/// when lzma_vli is assigned to a size_t.
static bool
is_direct_mode_needed(lzma_vli size)
{
	return size == LZMA_VLI_UNKNOWN || size > SIZE_MAX / 3;
}


static lzma_ret
stream_decoder_reset(struct lzma_stream_coder *coder,
		const lzma_allocator *allocator)
{
	// Initialize the Index hash used to verify the Index.
	coder->index_hash = lzma_index_hash_init(coder->index_hash, allocator);
	if (coder->index_hash == NULL)
		return LZMA_MEM_ERROR;

	// Reset the rest of the variables.
	coder->sequence = SEQ_STREAM_HEADER;
	coder->pos = 0;

	return LZMA_OK;
}


static lzma_ret
stream_decode_mt(void *coder_ptr, const lzma_allocator *allocator,
		 const uint8_t *restrict in, size_t *restrict in_pos,
		 size_t in_size,
		 uint8_t *restrict out, size_t *restrict out_pos,
		 size_t out_size, lzma_action action)
{
	struct lzma_stream_coder *coder = coder_ptr;

	const size_t in_start = *in_pos;

	mythread_condtime wait_abs;
	bool has_blocked = false;

	while (true)
	switch (coder->sequence) {
	case SEQ_STREAM_HEADER: {
		// Copy the Stream Header to the internal buffer.
		const size_t in_old = *in_pos;
		lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
				LZMA_STREAM_HEADER_SIZE);
		coder->progress_in += *in_pos - in_old;

		// Return if we didn't get the whole Stream Header yet.
		if (coder->pos < LZMA_STREAM_HEADER_SIZE)
			return LZMA_OK;

		coder->pos = 0;

		// Decode the Stream Header.
		const lzma_ret ret = lzma_stream_header_decode(
				&coder->stream_flags, coder->buffer);
		if (ret != LZMA_OK)
			return ret == LZMA_FORMAT_ERROR && !coder->first_stream
					? LZMA_DATA_ERROR : ret;

		// If we are decoding concatenated Streams, and the later
		// Streams have invalid Header Magic Bytes, we give
		// LZMA_DATA_ERROR instead of LZMA_FORMAT_ERROR.
		coder->first_stream = false;

		// Copy the type of the Check so that Block Header and Block
		// decoders see it.
		coder->block_options.check = coder->stream_flags.check;

		// Even if we return LZMA_*_CHECK below, we want
		// to continue from Block Header decoding.
		coder->sequence = SEQ_BLOCK_HEADER;

		// Detect if there's no integrity check or if it is
		// unsupported if those were requested by the application.
		if (coder->tell_no_check && coder->stream_flags.check
				== LZMA_CHECK_NONE)
			return LZMA_NO_CHECK;

		if (coder->tell_unsupported_check
				&& !lzma_check_is_supported(
					coder->stream_flags.check))
			return LZMA_UNSUPPORTED_CHECK;

		if (coder->tell_any_check)
			return LZMA_GET_CHECK;
	}

	// Fall through

	case SEQ_BLOCK_HEADER: {
		const size_t in_old = *in_pos;
		const lzma_ret ret = decode_block_header(coder, allocator,
				in, in_pos, in_size);
		coder->progress_in += *in_pos - in_old;

		if (ret == LZMA_OK) {
			// We didn't decode the whole Block Header yet.
			//
			// Read output from the queue before returning. This
			// is important because it is possible that the
			// application doesn't have any new input available
			// immediately. If we didn't try to copy output from
			// the output queue here, lzma_code() could end up
			// returning LZMA_BUF_ERROR even though queued output
			// is available.
			//
			// If the lzma_code() call provided at least one input
			// byte, only copy as much data from the output queue
			// as is available immediately. This way the
			// application will be able to provide more input
			// without a delay.
			//
			// On the other hand, if lzma_code() was called with
			// an empty input buffer (in_start == in_size), treat
			// it specially: try to fill the output buffer even
			// if it requires waiting for the worker threads to
			// provide output (timeout, if specified, can still
			// cause us to return).
			//
			//   - This way the application will be able to get all
			//     data that can be decoded from the input provided
			//     so far.
			//
			//   - We avoid both premature LZMA_BUF_ERROR and
			//     busy-waiting where the application repeatedly
			//     calls lzma_code() which immediately returns
			//     LZMA_OK without providing new data.
			//
			//   - If the queue becomes empty, we won't wait
			//     anything and will return LZMA_OK immediately
			//     (coder->timeout is completely ignored).
			//
			assert(*in_pos == in_size);

			return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				NULL, in_start == in_size,
				&wait_abs, &has_blocked));

			if (coder->pending_error != LZMA_OK) {
				coder->sequence = SEQ_ERROR;
				break;
			}

			return LZMA_OK;
		}

		if (ret == LZMA_INDEX_DETECTED) {
			coder->sequence = SEQ_INDEX_WAIT_OUTPUT;
			break;
		}

		// See if an error occurred.
		if (ret != LZMA_STREAM_END) {
			if (coder->pending_error == LZMA_OK)
				coder->pending_error = ret;

			coder->sequence = SEQ_ERROR;
			break;
		}

		// Calculate the memory usage of the filters / Block decoder.
		coder->mem_next_filters = lzma_raw_decoder_memusage(
				coder->filters);

		if (coder->mem_next_filters == UINT64_MAX) {
			// One or more unknown Filter IDs.
			if (coder->pending_error == LZMA_OK)
				coder->pending_error = LZMA_OPTIONS_ERROR;

			coder->sequence = SEQ_ERROR;
			break;
		}

		coder->sequence = SEQ_BLOCK_INIT;
	}

	// Fall through

	case SEQ_BLOCK_INIT: {
		// Check if decoding is possible at all with the current
		// memlimit_stop which we must never exceed.
		//
		// This needs to be the first thing in SEQ_BLOCK_INIT
		// to make it possible to restart decoding after increasing
		// memlimit_stop with lzma_memlimit_set().
		if (coder->mem_next_filters > coder->memlimit_stop) {
			// Flush pending output before returning
			// LZMA_MEMLIMIT_ERROR. If the application doesn't
			// want to increase the limit, at least it will get
			// all the output possible so far.
			return_if_error(read_output_and_wait(coder, allocator,
					out, out_pos, out_size,
					NULL, true, &wait_abs, &has_blocked));

			if (!lzma_outq_is_empty(&coder->outq))
				return LZMA_OK;

			return LZMA_MEMLIMIT_ERROR;
		}

		// Check if the size information is available in Block Header.
		// If it is, check if the sizes are small enough that we don't
		// need to worry *too* much about integer overflows later in
		// the code. If these conditions are not met, we must use the
		// single-threaded direct mode.
		if (is_direct_mode_needed(coder->block_options.compressed_size)
				|| is_direct_mode_needed(
				coder->block_options.uncompressed_size)) {
			coder->sequence = SEQ_BLOCK_DIRECT_INIT;
			break;
		}

		// Calculate the amount of memory needed for the input and
		// output buffers in threaded mode.
		//
		// These cannot overflow because we already checked that
		// the sizes are small enough using is_direct_mode_needed().
		coder->mem_next_in = comp_blk_size(coder);
		const uint64_t mem_buffers = coder->mem_next_in
				+ lzma_outq_outbuf_memusage(
				coder->block_options.uncompressed_size);

		// Add the amount needed by the filters.
		// Avoid integer overflows.
		if (UINT64_MAX - mem_buffers < coder->mem_next_filters) {
			// Use direct mode if the memusage would overflow.
			// This is a theoretical case that shouldn't happen
			// in practice unless the input file is weird (broken
			// or malicious).
			coder->sequence = SEQ_BLOCK_DIRECT_INIT;
			break;
		}

		// Amount of memory needed to decode this Block in
		// threaded mode:
		coder->mem_next_block = coder->mem_next_filters + mem_buffers;

		// If this alone would exceed memlimit_threading, then we must
		// use the single-threaded direct mode.
		if (coder->mem_next_block > coder->memlimit_threading) {
			coder->sequence = SEQ_BLOCK_DIRECT_INIT;
			break;
		}

		// Use the threaded mode. Free the direct mode decoder in
		// case it has been initialized.
		lzma_next_end(&coder->block_decoder, allocator);
		coder->mem_direct_mode = 0;

		// Since we already know what the sizes are supposed to be,
		// we can already add them to the Index hash. The Block
		// decoder will verify the values while decoding.
		const lzma_ret ret = lzma_index_hash_append(coder->index_hash,
				lzma_block_unpadded_size(
					&coder->block_options),
				coder->block_options.uncompressed_size);
		if (ret != LZMA_OK) {
			if (coder->pending_error == LZMA_OK)
				coder->pending_error = ret;

			coder->sequence = SEQ_ERROR;
			break;
		}

		coder->sequence = SEQ_BLOCK_THR_INIT;
	}

	// Fall through

	case SEQ_BLOCK_THR_INIT: {
		// We need to wait for a multiple conditions to become true
		// until we can initialize the Block decoder and let a worker
		// thread decode it:
		//
		//   - Wait for the memory usage of the active threads to drop
		//     so that starting the decoding of this Block won't make
		//     us go over memlimit_threading.
		//
		//   - Wait for at least one free output queue slot.
		//
		//   - Wait for a free worker thread.
		//
		// While we wait, we must copy decompressed data to the out
		// buffer and catch possible decoder errors.
		//
		// read_output_and_wait() does all the above.
		bool block_can_start = false;

		return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				&block_can_start, true,
				&wait_abs, &has_blocked));

		if (coder->pending_error != LZMA_OK) {
			coder->sequence = SEQ_ERROR;
			break;
		}

		if (!block_can_start) {
			// It's not a timeout because return_if_error handles
			// it already. Output queue cannot be empty either
			// because in that case block_can_start would have
			// been true. Thus the output buffer must be full and
			// the queue isn't empty.
			assert(*out_pos == out_size);
			assert(!lzma_outq_is_empty(&coder->outq));
			return LZMA_OK;
		}

		// We know that we can start decoding this Block without
		// exceeding memlimit_threading. However, to stay below
		// memlimit_threading may require freeing some of the
		// cached memory.
		//
		// Get a local copy of variables that require locking the
		// mutex. It is fine if the worker threads modify the real
		// values after we read these as those changes can only be
		// towards more favorable conditions (less memory in use,
		// more in cache).
		uint64_t mem_in_use;
		uint64_t mem_cached;
		struct worker_thread *thr;

		mythread_sync(coder->mutex) {
			mem_in_use = coder->mem_in_use;
			mem_cached = coder->mem_cached;
			thr = coder->threads_free;
		}

		// The maximum amount of memory that can be held by other
		// threads and cached buffers while allowing us to start
		// decoding the next Block.
		const uint64_t mem_max = coder->memlimit_threading
				- coder->mem_next_block;

		// If the existing allocations are so large that starting
		// to decode this Block might exceed memlimit_threads,
		// try to free memory from the output queue cache first.
		//
		// NOTE: This math assumes the worst case. It's possible
		// that the limit wouldn't be exceeded if the existing cached
		// allocations are reused.
		if (mem_in_use + mem_cached + coder->outq.mem_allocated
				> mem_max) {
			// Clear the outq cache except leave one buffer in
			// the cache if its size is correct. That way we
			// don't free and almost immediately reallocate
			// an identical buffer.
			lzma_outq_clear_cache2(&coder->outq, allocator,
				coder->block_options.uncompressed_size);
		}

		// If there is at least one worker_thread in the cache and
		// the existing allocations are so large that starting to
		// decode this Block might exceed memlimit_threads, free
		// memory by freeing cached Block decoders.
		//
		// NOTE: The comparison is different here than above.
		// Here we don't care about cached buffers in outq anymore
		// and only look at memory actually in use. This is because
		// if there is something in outq cache, it's a single buffer
		// that can be used as is. We ensured this in the above
		// if-block.
		uint64_t mem_freed = 0;
		if (thr != NULL && mem_in_use + mem_cached
				+ coder->outq.mem_in_use > mem_max) {
			// Don't free the first Block decoder if its memory
			// usage isn't greater than what this Block will need.
			// Typically the same filter chain is used for all
			// Blocks so this way the allocations can be reused
			// when get_thread() picks the first worker_thread
			// from the cache.
			if (thr->mem_filters <= coder->mem_next_filters)
				thr = thr->next;

			while (thr != NULL) {
				lzma_next_end(&thr->block_decoder, allocator);
				mem_freed += thr->mem_filters;
				thr->mem_filters = 0;
				thr = thr->next;
			}
		}

		// Update the memory usage counters. Note that coder->mem_*
		// may have changed since we read them so we must substract
		// or add the changes.
		mythread_sync(coder->mutex) {
			coder->mem_cached -= mem_freed;

			// Memory needed for the filters and the input buffer.
			// The output queue takes care of its own counter so
			// we don't touch it here.
			//
			// NOTE: After this, coder->mem_in_use +
			// coder->mem_cached might count the same thing twice.
			// If so, this will get corrected in get_thread() when
			// a worker_thread is picked from coder->free_threads
			// and its memory usage is substracted from mem_cached.
			coder->mem_in_use += coder->mem_next_in
					+ coder->mem_next_filters;
		}

		// Allocate memory for the output buffer in the output queue.
		return_if_error(lzma_outq_prealloc_buf(
				&coder->outq, allocator,
				coder->block_options.uncompressed_size));

		// Set up coder->thr.
		return_if_error(get_thread(coder, allocator));

		// The new Block decoder memory usage is already counted in
		// coder->mem_in_use. Store it in the thread too.
		coder->thr->mem_filters = coder->mem_next_filters;

		// Initialize the Block decoder.
		coder->thr->block_options = coder->block_options;
		const lzma_ret ret = lzma_block_decoder_init(
					&coder->thr->block_decoder, allocator,
					&coder->thr->block_options);

		// Free the allocated filter options since they are needed
		// only to initialize the Block decoder.
		cleanup_filters(coder->filters, allocator);
		coder->thr->block_options.filters = NULL;

		// Check if memory usage calculation and Block encoder
		// initialization succeeded.
		if (ret != LZMA_OK) {
			if (coder->pending_error == LZMA_OK)
				coder->pending_error = ret;

			coder->sequence = SEQ_ERROR;
			break;
		}

		// Allocate the input buffer.
		coder->thr->in_size = coder->mem_next_in;
		coder->thr->in = lzma_alloc(coder->thr->in_size, allocator);
		if (coder->thr->in == NULL)
			return LZMA_MEM_ERROR;

		// Get the preallocated output buffer.
		coder->thr->outbuf = lzma_outq_get_buf(
				&coder->outq, coder->thr);

		// Start the decoder.
		mythread_sync(coder->thr->mutex) {
			assert(coder->thr->state == THR_IDLE);
			coder->thr->state = THR_RUN;
			mythread_cond_signal(&coder->thr->cond);
		}

		// Enable output from the thread that holds the oldest output
		// buffer in the output queue (if such a thread exists).
		mythread_sync(coder->mutex) {
			lzma_outq_enable_partial_output(&coder->outq,
					&worker_enable_partial_update);
		}

		coder->sequence = SEQ_BLOCK_THR_RUN;
	}

	// Fall through

	case SEQ_BLOCK_THR_RUN: {
		// Copy input to the worker thread.
		size_t cur_in_filled = coder->thr->in_filled;
		lzma_bufcpy(in, in_pos, in_size, coder->thr->in,
				&cur_in_filled, coder->thr->in_size);

		// Tell the thread how much we copied.
		mythread_sync(coder->thr->mutex) {
			coder->thr->in_filled = cur_in_filled;

			// NOTE: Most of the time we are copying input faster
			// than the thread can decode so most of the time
			// calling mythread_cond_signal() is useless but
			// we cannot make it conditional because thr->in_pos
			// is updated without a mutex. And the overhead should
			// be very much negligible anyway.
			mythread_cond_signal(&coder->thr->cond);
		}

		// Read output from the output queue. Just like in
		// SEQ_BLOCK_HEADER, we wait to fill the output buffer
		// only if lzma_code() was called without providing any input.
		return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				NULL, in_start == in_size,
				&wait_abs, &has_blocked));

		if (coder->pending_error != LZMA_OK) {
			coder->sequence = SEQ_ERROR;
			break;
		}

		// Return if the input didn't contain the whole Block.
		if (coder->thr->in_filled < coder->thr->in_size) {
			assert(*in_pos == in_size);
			return LZMA_OK;
		}

		// The whole Block has been copied to the thread-specific
		// buffer. Continue from the next Block Header or Index.
		coder->thr = NULL;
		coder->sequence = SEQ_BLOCK_HEADER;
		break;
	}

	case SEQ_BLOCK_DIRECT_INIT: {
		// Wait for the threads to finish and that all decoded data
		// has been copied to the output. That is, wait until the
		// output queue becomes empty.
		//
		// NOTE: No need to check for coder->pending_error as
		// we aren't consuming any input until the queue is empty
		// and if there is a pending error, read_output_and_wait()
		// will eventually return it before the queue is empty.
		return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				NULL, true, &wait_abs, &has_blocked));
		if (!lzma_outq_is_empty(&coder->outq))
			return LZMA_OK;

		// Free the cached output buffers.
		lzma_outq_clear_cache(&coder->outq, allocator);

		// Get rid of the worker threads, including the coder->threads
		// array.
		threads_end(coder, allocator);

		// Initialize the Block decoder.
		const lzma_ret ret = lzma_block_decoder_init(
				&coder->block_decoder, allocator,
				&coder->block_options);

		// Free the allocated filter options since they are needed
		// only to initialize the Block decoder.
		cleanup_filters(coder->filters, allocator);
		coder->block_options.filters = NULL;

		// Check if Block decoder initialization succeeded.
		if (ret != LZMA_OK)
			return ret;

		// Make the memory usage visible to _memconfig().
		coder->mem_direct_mode = coder->mem_next_filters;

		coder->sequence = SEQ_BLOCK_DIRECT_RUN;
	}

	// Fall through

	case SEQ_BLOCK_DIRECT_RUN: {
		const size_t in_old = *in_pos;
		const size_t out_old = *out_pos;
		const lzma_ret ret = coder->block_decoder.code(
				coder->block_decoder.coder, allocator,
				in, in_pos, in_size, out, out_pos, out_size,
				action);
		coder->progress_in += *in_pos - in_old;
		coder->progress_out += *out_pos - out_old;

		if (ret != LZMA_STREAM_END)
			return ret;

		// Block decoded successfully. Add the new size pair to
		// the Index hash.
		return_if_error(lzma_index_hash_append(coder->index_hash,
				lzma_block_unpadded_size(
					&coder->block_options),
				coder->block_options.uncompressed_size));

		coder->sequence = SEQ_BLOCK_HEADER;
		break;
	}

	case SEQ_INDEX_WAIT_OUTPUT:
		// Flush the output from all worker threads so that we can
		// decode the Index without thinking about threading.
		return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				NULL, true, &wait_abs, &has_blocked));

		if (!lzma_outq_is_empty(&coder->outq))
			return LZMA_OK;

		coder->sequence = SEQ_INDEX_DECODE;

	// Fall through

	case SEQ_INDEX_DECODE: {
		// If we don't have any input, don't call
		// lzma_index_hash_decode() since it would return
		// LZMA_BUF_ERROR, which we must not do here.
		if (*in_pos >= in_size)
			return LZMA_OK;

		// Decode the Index and compare it to the hash calculated
		// from the sizes of the Blocks (if any).
		const size_t in_old = *in_pos;
		const lzma_ret ret = lzma_index_hash_decode(coder->index_hash,
				in, in_pos, in_size);
		coder->progress_in += *in_pos - in_old;
		if (ret != LZMA_STREAM_END)
			return ret;

		coder->sequence = SEQ_STREAM_FOOTER;
	}

	// Fall through

	case SEQ_STREAM_FOOTER: {
		// Copy the Stream Footer to the internal buffer.
		const size_t in_old = *in_pos;
		lzma_bufcpy(in, in_pos, in_size, coder->buffer, &coder->pos,
				LZMA_STREAM_HEADER_SIZE);
		coder->progress_in += *in_pos - in_old;

		// Return if we didn't get the whole Stream Footer yet.
		if (coder->pos < LZMA_STREAM_HEADER_SIZE)
			return LZMA_OK;

		coder->pos = 0;

		// Decode the Stream Footer. The decoder gives
		// LZMA_FORMAT_ERROR if the magic bytes don't match,
		// so convert that return code to LZMA_DATA_ERROR.
		lzma_stream_flags footer_flags;
		const lzma_ret ret = lzma_stream_footer_decode(
				&footer_flags, coder->buffer);
		if (ret != LZMA_OK)
			return ret == LZMA_FORMAT_ERROR
					? LZMA_DATA_ERROR : ret;

		// Check that Index Size stored in the Stream Footer matches
		// the real size of the Index field.
		if (lzma_index_hash_size(coder->index_hash)
				!= footer_flags.backward_size)
			return LZMA_DATA_ERROR;

		// Compare that the Stream Flags fields are identical in
		// both Stream Header and Stream Footer.
		return_if_error(lzma_stream_flags_compare(
				&coder->stream_flags, &footer_flags));

		if (!coder->concatenated)
			return LZMA_STREAM_END;

		coder->sequence = SEQ_STREAM_PADDING;
	}

	// Fall through

	case SEQ_STREAM_PADDING:
		assert(coder->concatenated);

		// Skip over possible Stream Padding.
		while (true) {
			if (*in_pos >= in_size) {
				// Unless LZMA_FINISH was used, we cannot
				// know if there's more input coming later.
				if (action != LZMA_FINISH)
					return LZMA_OK;

				// Stream Padding must be a multiple of
				// four bytes.
				return coder->pos == 0
						? LZMA_STREAM_END
						: LZMA_DATA_ERROR;
			}

			// If the byte is not zero, it probably indicates
			// beginning of a new Stream (or the file is corrupt).
			if (in[*in_pos] != 0x00)
				break;

			++*in_pos;
			++coder->progress_in;
			coder->pos = (coder->pos + 1) & 3;
		}

		// Stream Padding must be a multiple of four bytes (empty
		// Stream Padding is OK).
		if (coder->pos != 0) {
			++*in_pos;
			++coder->progress_in;
			return LZMA_DATA_ERROR;
		}

		// Prepare to decode the next Stream.
		return_if_error(stream_decoder_reset(coder, allocator));
		break;

	case SEQ_ERROR:
		// Let the application get all data before the point where
		// the error was detected. This matches the behavior of
		// single-threaded use.
		//
		// FIXME? Some errors (LZMA_MEM_ERROR) don't get here,
		// they are returned immediately. Thus in rare cases the
		// output will be less than in single-threaded mode. But
		// maybe this doesn't matter much in practice.
		return_if_error(read_output_and_wait(coder, allocator,
				out, out_pos, out_size,
				NULL, true, &wait_abs, &has_blocked));

		// We get here only if the error happened in the main thread,
		// for example, unsupported Block Header.
		if (!lzma_outq_is_empty(&coder->outq))
			return LZMA_OK;

		return coder->pending_error;

	default:
		assert(0);
		return LZMA_PROG_ERROR;
	}

	// Never reached
}


static void
stream_decoder_mt_end(void *coder_ptr, const lzma_allocator *allocator)
{
	struct lzma_stream_coder *coder = coder_ptr;

	threads_end(coder, allocator);
	lzma_outq_end(&coder->outq, allocator);

	lzma_next_end(&coder->block_decoder, allocator);
	cleanup_filters(coder->filters, allocator);
	lzma_index_hash_end(coder->index_hash, allocator);

	lzma_free(coder, allocator);
	return;
}


static lzma_check
stream_decoder_mt_get_check(const void *coder_ptr)
{
	const struct lzma_stream_coder *coder = coder_ptr;
	return coder->stream_flags.check;
}


static lzma_ret
stream_decoder_mt_memconfig(void *coder_ptr, uint64_t *memusage,
		uint64_t *old_memlimit, uint64_t new_memlimit)
{
	// NOTE: This function gets/sets memlimit_stop. For now,
	// memlimit_threading cannot be modified after initialization.
	struct lzma_stream_coder *coder = coder_ptr;

	mythread_sync(coder->mutex) {
		*memusage = coder->mem_direct_mode + coder->mem_in_use
				+ coder->outq.mem_in_use; // FIXME?
	}

	// If no filter chains are allocated, *memusage may be zero.
	// Always return at least LZMA_MEMUSAGE_BASE.
	if (*memusage < LZMA_MEMUSAGE_BASE)
		*memusage = LZMA_MEMUSAGE_BASE;

	*old_memlimit = coder->memlimit_stop;

	if (new_memlimit != 0) {
		if (new_memlimit < *memusage) // FIXME?
			return LZMA_MEMLIMIT_ERROR;

		coder->memlimit_stop = new_memlimit;
	}

	return LZMA_OK;
}


static void
stream_decoder_mt_get_progress(void *coder_ptr,
		uint64_t *progress_in, uint64_t *progress_out)
{
	struct lzma_stream_coder *coder = coder_ptr;

	// Lock coder->mutex to prevent finishing threads from moving their
	// progress info from the worker_thread structure to lzma_stream_coder.
	mythread_sync(coder->mutex) {
		*progress_in = coder->progress_in;
		*progress_out = coder->progress_out;

		for (size_t i = 0; i < coder->threads_initialized; ++i) {
			mythread_sync(coder->threads[i].mutex) {
				*progress_in += coder->threads[i].progress_in;
				*progress_out += coder->threads[i]
						.progress_out;
			}
		}
	}

	return;
}


static lzma_ret
stream_decoder_mt_init(lzma_next_coder *next, const lzma_allocator *allocator,
		       const lzma_mt *options)
{
	struct lzma_stream_coder *coder;

	if (options->threads == 0 || options->threads > LZMA_THREADS_MAX)
		return LZMA_OPTIONS_ERROR;

	if (options->flags & ~LZMA_SUPPORTED_FLAGS)
		return LZMA_OPTIONS_ERROR;

	lzma_next_coder_init(&stream_decoder_mt_init, next, allocator);

	coder = next->coder;
	if (!coder) {
		coder = lzma_alloc(sizeof(struct lzma_stream_coder), allocator);
		if (coder == NULL)
			return LZMA_MEM_ERROR;

		next->coder = coder;

		if (mythread_mutex_init(&coder->mutex)) {
			lzma_free(coder, allocator);
			return LZMA_MEM_ERROR;
		}

		if (mythread_cond_init(&coder->cond)) {
			mythread_mutex_destroy(&coder->mutex);
			lzma_free(coder, allocator);
			return LZMA_MEM_ERROR;
		}

		next->code = &stream_decode_mt;
		next->end = &stream_decoder_mt_end;
		next->get_check = &stream_decoder_mt_get_check;
		next->memconfig = &stream_decoder_mt_memconfig;
		next->get_progress = &stream_decoder_mt_get_progress;

		memzero(coder->filters, sizeof(coder->filters));
		memzero(&coder->outq, sizeof(coder->outq));

		coder->block_decoder = LZMA_NEXT_CODER_INIT;
		coder->mem_direct_mode = 0;

		coder->index_hash = NULL;
		coder->threads = NULL;
		coder->threads_free = NULL;
		coder->threads_initialized = 0;
	}

	// Cleanup old filter chain if one remains after unfinished decoding
	// of a previous Stream.
	cleanup_filters(coder->filters, allocator);

	// By allocating threads from scratch we can start memory-usage
	// accounting from scratch, too. Changes in filter and block sizes may
	// affect number of threads.
	//
	// FIXME? Reusing should be easy but unlike the single-threaded
	// decoder, with some types of input file combinations reusing
	// could leave quite a lot of memory allocated but unused (first
	// file could allocate a lot, the next files could use fewer
	// threads and some of the allocations from the first file would not
	// get freed unless memlimit_threading forces us to clear caches).
	//
	// NOTE: The direct mode decoder isn't freed here if one exists.
	// It will be reused or freed as needed in the main loop.
	threads_end(coder, allocator);

	// All memusage counters start at 0 (including mem_direct_mode).
	// The little extra that is needed for the structs in this file
	// get accounted well enough by the filter chain memory usage
	// which adds LZMA_MEMUSAGE_BASE for each chain. However,
	// stream_decoder_mt_memconfig() has to handle this specially so that
	// it will never return less than LZMA_MEMUSAGE_BASE as memory usage.
	coder->mem_in_use = 0;
	coder->mem_cached = 0;
	coder->mem_next_block = 0;

	coder->progress_in = 0;
	coder->progress_out = 0;

	coder->sequence = SEQ_STREAM_HEADER;
	coder->thread_error = LZMA_OK;
	coder->pending_error = LZMA_OK;
	coder->thr = NULL;

	coder->timeout = options->timeout;

	coder->memlimit_threading = my_max(1, options->memlimit_threading);
	coder->memlimit_stop = my_max(1, options->memlimit_stop);
	if (coder->memlimit_threading > coder->memlimit_stop)
		coder->memlimit_threading = coder->memlimit_stop;

	coder->tell_no_check = (options->flags & LZMA_TELL_NO_CHECK) != 0;
	coder->tell_unsupported_check
			= (options->flags & LZMA_TELL_UNSUPPORTED_CHECK) != 0;
	coder->tell_any_check = (options->flags & LZMA_TELL_ANY_CHECK) != 0;
	coder->ignore_check = (options->flags & LZMA_IGNORE_CHECK) != 0;
	coder->concatenated = (options->flags & LZMA_CONCATENATED) != 0;
	coder->first_stream = true;
	coder->pos = 0;

	coder->threads_max = options->threads;

	return_if_error(lzma_outq_init(&coder->outq, allocator,
				       coder->threads_max));

	return stream_decoder_reset(coder, allocator);
}


extern LZMA_API(lzma_ret)
lzma_stream_decoder_mt(lzma_stream *strm, const lzma_mt *options)
{
	lzma_next_strm_init(stream_decoder_mt_init, strm, options);

	strm->internal->supported_actions[LZMA_RUN] = true;
	strm->internal->supported_actions[LZMA_FINISH] = true;

	return LZMA_OK;
}
