///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_utils.c
/// \brief      Provides test util function implementations
//
//  Author:     TODO
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "test_utils.h"
#include "stest.h"
#include "config.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#	include <windows.h>
#endif

#if HAVE_GLOB_H
#	include <glob.h>
#endif

/*
 * Call standard system() call, but build up the command line using
 * sprintf() conventions.
 *
 * Function taken and adapted from Tim Kientzle in Libarchive
 */
int
systemf(const char *fmt, ...)
{
	char buff[8192];
	va_list ap;
	int r;

	va_start(ap, fmt);
	vsprintf(buff, fmt, ap);
	r = system(buff);
	va_end(ap);
	return (r);
}

bool
can_xz(void)
{
	return file_exists_and_can_execute(XZ_ABS_PATH);
}

bool
can_xz_dec(void)
{
	return file_exists_and_can_execute(XZ_DEC_ABS_PATH);
}

bool
can_glob(void)
{
#if defined(_WIN32) || defined(__CYGWIN__) || HAVE_GLOB_H
	return true;
#else
	return false;
#endif

}


void
sleep_ms(int ms)
{
#if defined(_WIN32)
	Sleep(ms);
#else
	usleep(ms * 1000);
#endif
}

bool
file_exists_and_can_execute(const char* path)
{
	return access(path, X_OK) == 0;
}

bool
file_exists_and_can_read(const char* path)
{
	return access(path, R_OK) == 0;
}

void
glob_and_callback(const char* path, glob_callback callback)
{
#if HAVE_GLOB_H
	glob_t globbing;
	int glob_ret = glob(path, 0, NULL, &globbing);
	assert_false(glob_ret);
	if(!glob_ret){
		for(unsigned int i = 0; i < globbing.gl_pathc; i++){
			char* filename = globbing.gl_pathv[i];
			callback(filename);
		}
	}
#elif defined(_WIN32) || defined(__CYGWIN__)
	HANDLE h_find;
	WIN32_FIND_DATA find_file;
	h_find = FindFirstFile(path, &find_file);
	if (h_find == INVALID_HANDLE_VALUE){
		return 1;
	}
	do {
		callback(find_file.cFileName);
		printf("\n");
	}
	while(FindNextFile(h_find, &find_file) !=0);

	FindClose(h_find);
#endif
}

// Path is the location to the file
// Buffer will be allocated and must be freed by the caller
// when they are done using the buffer
size_t
read_file_into_buffer(const char* path, uint8_t** buffer_ptr)
{
	size_t file_size = 0;
	if(!file_exists_and_can_read(path)){
		return file_size;
	}

	static size_t buffer_chunk_size = 4096;
	uint8_t* buffer = (uint8_t*) malloc(buffer_chunk_size);
	if(buffer == NULL){
		return file_size;
	}
	int chunk_count = 1;

	FILE* file = fopen(path, "r");
	if(file == NULL){
		return file_size;
	}

	while(!feof(file)){
		size_t read = fread(buffer +
				((chunk_count-1) * buffer_chunk_size),
				1, buffer_chunk_size, file);
		chunk_count++;
		file_size += read;
		//printf("chunk count %d file size %d\n", chunk_count, file_size);
		if(read == buffer_chunk_size){
			buffer = realloc(buffer,
				 	buffer_chunk_size * chunk_count);
			if(buffer == NULL){
				return 0;
			}
		}
	}

	*buffer_ptr = buffer;
	return file_size;
}


bool
prepare_test_file_data(test_file_data *data)
{
	if (data->compressed_filename != NULL) {
		data->compressed_size = read_file_into_buffer(
				data->compressed_filename,
				&data->compressed_data);
		if (data->compressed_size == 0)
			return false;
	}

	if (data->plain_filename != NULL){
		data->plain_size = read_file_into_buffer(
				data->plain_filename,
				&data->plain_data);
		if (data->plain_size == 0)
			return false;
	}

	return true;
}


void
free_test_file_data(test_file_data *data)
{
	if (data->compressed_filename != NULL)
		free(data->compressed_data);
	if (data->plain_filename != NULL)
		free(data->plain_data);
}
