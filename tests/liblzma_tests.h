///////////////////////////////////////////////////////////////////////////////
//
/// \file       liblzma_tests.h
/// \brief      Provides headers for all tests for liblzma
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef LIBLZMA_TESTS_H
#define LIBLZMA_TESTS_H

#include "lzma.h"
#include "stest.h"

void test_integrity_checks(void);
void test_block_headers(void);
void test_bcj_filter(void);
void test_filter_flags(void);
void test_lzma_index_structure(void);
void test_stream_header_and_footer_coders(void);
void test_lzma_filters(void);
void test_lzma_raw(void);
void test_lzma_properties(void);
void test_mt_decoder(void);
void test_block(void);

#endif
