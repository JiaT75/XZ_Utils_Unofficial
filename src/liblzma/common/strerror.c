///////////////////////////////////////////////////////////////////////////////
//
/// \file       strerror.c
/// \brief      Convert lzma_ret to human readable text
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "tuklib_gettext.h"

#define LZMA_SUCCESS_STR "Liblzma Success: "
#define LZMA_WARNING_STR "Liblmza Warning: "
#define LZMA_FATAL_STR "Liblzma Fatal: "

// See src/liblzma/api/lzma/base.h for a full list and 
// description of the status codes
extern LZMA_API(const char *) lzma_strerror(lzma_ret code,
		lzma_bool is_encoder)
{
	switch (code) {
		case LZMA_OK:
			return _(LZMA_SUCCESS_STR "Operation completed");
		case LZMA_STREAM_END:
			if (is_encoder)
				return _(LZMA_SUCCESS_STR "Compressed data "
					"flush completed");
			else
				return _(LZMA_SUCCESS_STR "All data "
					"decompressed");
		case LZMA_NO_CHECK:
			return _(LZMA_WARNING_STR "Input stream has no "
				"integrity check");
		case LZMA_UNSUPPORTED_CHECK:
			if (is_encoder)
				return _(LZMA_FATAL_STR "Unsupported"
					"integrity check");
			else
				return _(LZMA_WARNING_STR "Unsupported "
					"integrity check. Decompression "
					"can continue, but errors may go "
					"undetected");
		case LZMA_GET_CHECK:
			return _(LZMA_SUCCESS_STR "lzma_get_check can now "
				"be called to determine the Check ID "
				"value");
		case LZMA_MEM_ERROR:
			return _(LZMA_FATAL_STR "Cannot allocate memory");
		case LZMA_MEMLIMIT_ERROR:
			return _(LZMA_FATAL_STR "Memory limit reached");
		case LZMA_FORMAT_ERROR:
			return _(LZMA_FATAL_STR "file format not "
				"recognized");
		case LZMA_OPTIONS_ERROR:
			return _(LZMA_FATAL_STR "Invalid or unsupported "
				"options");
		case LZMA_DATA_ERROR:
			if (is_encoder)
				return _(LZMA_FATAL_STR "Size limit of "
					"target file exceeded");
			else
				return _(LZMA_FATAL_STR "Input data is "
					"corrupt");
		case LZMA_BUF_ERROR:
			return _(LZMA_WARNING_STR "No progress is "
				"possible. Cannot consume more input "
				"or create more output");
		case LZMA_PROG_ERROR:
			return _(LZMA_WARNING_STR "Programming error. "
				"Invalid arguments or coder internal state "
				"is corrupt");
		case LZMA_SEEK_NEEDED:
			return _(LZMA_WARNING_STR "File seek is needed. "
				"Seek to value in lzma_stream.seek_pos and "
				"the continue coding");
		case LZMA_RET_INTERNAL1:
		case LZMA_RET_INTERNAL2:
		case LZMA_RET_INTERNAL3:
		case LZMA_RET_INTERNAL4:
		case LZMA_RET_INTERNAL5:
		case LZMA_RET_INTERNAL6:
		case LZMA_RET_INTERNAL7:
		case LZMA_RET_INTERNAL8:
			break;
	}

	return _("Status code unrecognized");
}
