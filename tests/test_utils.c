///////////////////////////////////////////////////////////////////////////////
//
/// \file       test_utils.c
/// \brief      Provides test util function implementations
//
//  Author:     Jia Tan
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

bool
file_exists_and_can_execute(const char* path)
{
	return access(path, X_OK) == 0;
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

