///////////////////////////////////////////////////////////////////////////////
//
/// \file       block_buffer_encoder.h
/// \brief      Single-call .xz Block encoder
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LZMA_BLOCK_BUFFER_ENCODER_H
#define LZMA_BLOCK_BUFFER_ENCODER_H

#include "common.h"


/// uint64_t version of lzma_block_buffer_bound(). It is used by
/// stream_encoder_mt.c. Probably the original lzma_block_buffer_bound()
/// should have been 64-bit, but fixing it would break the ABI.
extern uint64_t lzma_block_buffer_bound64(uint64_t uncompressed_size);

/// Encodes block data as uncompressed lzma2 chunks
/// Does not encode headers or end markers
extern lzma_ret lzma_encode_data_uncomp(const uint8_t *in,
 		size_t in_size, uint8_t *out, size_t *out_pos,
		size_t out_size);

#endif
