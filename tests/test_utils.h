///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_utils.h
/// \brief      Test util definitions and function prototypes
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "config.h"

#define memcrap(buf, size) memset(buf, 0xFD, size)
#define MAX_PATH_LENGTH 1024

#define QUOTE_MACRO(macro) #macro
#define CREATE_SRC_PATH(s) QUOTE_MACRO(s)"/src/"
#define SRC_PATH CREATE_SRC_PATH(ROOT_DIR)

#if defined(MY_SUFFIX)
#	define XZ_ABS_PATH SRC_PATH "xz/xz" MY_SUFFIX
#	define XZ_DEC_ABS_PATH SRC_PATH "xzdec/xzdec" MY_SUFFIX
#else
#	define XZ_ABS_PATH SRC_PATH "xz/xz"
#	define XZ_DEC_ABS_PATH SRC_PATH "xzdec/xzdec"
#endif

typedef void (*glob_callback)(char* path);

typedef struct {
	uint8_t* compressed_data;
	uint8_t* plain_data;
	size_t compressed_size;
	size_t plain_size;
	const char* compressed_filename;
	const char* plain_filename;
} test_file_data;

int systemf(const char *fmt, ...);

bool can_xz(void);
bool can_xz_dec(void);
bool can_glob(void);

void glob_and_callback(const char* path, glob_callback callback);
void sleep_ms(int ms);

bool file_exists_and_can_execute(const char* path);
bool file_exists_and_can_read(const char* path);
void get_path_to_files(char* out_path);
size_t read_file_into_buffer(const char* path, uint8_t** buffer_ptr);

bool prepare_test_file_data(test_file_data *data);
void free_test_file_data(test_file_data *data);

#endif
