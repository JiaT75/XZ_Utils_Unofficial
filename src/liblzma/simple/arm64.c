///////////////////////////////////////////////////////////////////////////////
//
/// \file       arm64.c
/// \brief      Filter for ARM64 binaries
///
//  Authors:    Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "simple_private.h"


static size_t
arm64_code(void *simple lzma_attribute((__unused__)),
               uint32_t now_pos, bool is_encoder,
               uint8_t *buffer, size_t size)
{
       size_t i;
       for (i = 0; i + 4 <= size; i += 4) {
               // arm64 bl instruction: 0x94 and 0x97;
               if (buffer[i + 3] == 0x94 || buffer[i + 3] == 0x97) {
                       uint32_t src = ((uint32_t)(buffer[i + 2]) << 16)
                                       | ((uint32_t)(buffer[i + 1]) << 8)
                                       | (uint32_t)(buffer[i + 0]);
                       src <<= 2;

                       uint32_t dest;
                       if (is_encoder)
                               dest = now_pos + (uint32_t)(i) + src;
                       else
                               dest = src - (now_pos + (uint32_t)(i));

                       dest >>= 2;
                       buffer[i + 2] = (dest >> 16);
                       buffer[i + 1] = (dest >> 8);
                       buffer[i + 0] = dest;
               }
       }

       return i;
}


static lzma_ret
arm64_coder_init(lzma_next_coder *next, const lzma_allocator *allocator,
               const lzma_filter_info *filters, bool is_encoder)
{
       return lzma_simple_coder_init(next, allocator, filters,
                       &arm64_code, 0, 4, 4, is_encoder);
}


extern lzma_ret
lzma_simple_arm64_encoder_init(lzma_next_coder *next,
               const lzma_allocator *allocator,
               const lzma_filter_info *filters)
{
       return arm64_coder_init(next, allocator, filters, true);
}


extern lzma_ret
lzma_simple_arm64_decoder_init(lzma_next_coder *next,
               const lzma_allocator *allocator,
               const lzma_filter_info *filters)
{
       return arm64_coder_init(next, allocator, filters, false);
}
