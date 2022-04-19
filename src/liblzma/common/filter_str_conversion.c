///////////////////////////////////////////////////////////////////////////////
//
/// \file       filter_str_conversion.c
/// \brief      Functions for convert filter chains to and from strings
//
//  Author:     Jia Tan
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "common.h"
#include "stdio.h"

// There are no filter names longer than 10 characters
#define FILTER_NAME_MAX_SIZE 10
#define MAX_OPTION_NAME_LEN 20
#define MAX_OPTION_VALUE_LEN 10


// Writing out an option with three parts:
// key - the name of the option for the filter
// LZMA_FILTER_KEY_TO_VALUE_DELIMITER - the delimiter
//    value to signal the key is done being written
// value - the value of the option for the filter
// For example: "depth:200,"
// The final argument determines if the LZMA_FILTER_OPTION_DELIMITER
// should be included at the end
static lzma_ret
write_out_str_option(char *out_str, const char* option_name,
		const char* option_value, size_t max_str_len,
		size_t *out_pos, bool final)
{
	int option_val_len = strnlen(option_value, MAX_OPTION_VALUE_LEN);
	int option_name_len = strnlen(option_name, max_str_len);

	size_t projected_out_pos = option_name_len + option_val_len
			+ *out_pos + 1;
	if (!final)
		projected_out_pos++;

	if (projected_out_pos > max_str_len)
		return LZMA_BUF_ERROR;

	memcpy(out_str + *out_pos, option_name, option_name_len);
	*out_pos += option_name_len;
	out_str[(*out_pos)++] = LZMA_FILTER_KEY_TO_VALUE_DELIMITER;
	memcpy(out_str + *out_pos, option_value, option_val_len);
	*out_pos += option_val_len;
	if (!final) {
		out_str[(*out_pos)++] = LZMA_FILTER_OPTION_DELIMITER;
	}

	return LZMA_OK;
}


static const char *
uint32_to_optstr(uint32_t num)
{
	static char buf[16];

	if ((num & ((UINT32_C(1) << 20) - 1)) == 0)
		snprintf(buf, sizeof(buf), "%" PRIu32 "MiB", num >> 20);
	else if ((num & ((UINT32_C(1) << 10) - 1)) == 0)
		snprintf(buf, sizeof(buf), "%" PRIu32 "KiB", num >> 10);
	else
		snprintf(buf, sizeof(buf), "%" PRIu32, num);

	return buf;
}


static lzma_ret
write_out_num_option(char *out_str, const char* option_name,
		uint32_t option_value, size_t max_str_len,
		size_t *out_pos, bool final)
{
	char value_str[MAX_OPTION_VALUE_LEN];
	snprintf(value_str, sizeof(value_str), "%u", option_value);
	return write_out_str_option(out_str, option_name, value_str,
			max_str_len, out_pos, final);
}


static lzma_ret
stringify_lzma_filter(lzma_filter *filter, char *out_str,
		size_t max_str_len, size_t *out_pos, const char* name)
{
	// Not comparing to any presets, so must
	// always include options
	int name_len = strnlen(name, max_str_len);
	if ((*out_pos + name_len + 1) > max_str_len)
		return LZMA_BUF_ERROR;

	memcpy(out_str + *out_pos, name, name_len);
	*out_pos += name_len;
	// Write out indicator
	out_str[(*out_pos)++] = LZMA_FILTER_OPTIONS_LIST_INDICATOR;

	lzma_options_lzma *options = (lzma_options_lzma *) filter->options;

	// Only copy over options if they != the default value
	if (options->dict_size != LZMA_DICT_SIZE_DEFAULT) {
		const char* value = uint32_to_optstr(options->dict_size);
		return_if_error(write_out_str_option(out_str,
				LZMA_DICT_SIZE_STR, value,
				max_str_len, out_pos, false));
	}

	if (options->lc != LZMA_LC_DEFAULT) {
		return_if_error(write_out_num_option(out_str, LZMA_LC_STR,
				options->lc, max_str_len,
				out_pos, false));
	}

	if (options->lp != LZMA_LP_DEFAULT) {
		return_if_error(write_out_num_option(out_str, LZMA_LP_STR,
				options->lp, max_str_len,
				out_pos, false));
	}

	if (options->pb != LZMA_PB_DEFAULT) {
		return_if_error(write_out_num_option(out_str, LZMA_PB_STR,
				options->pb, max_str_len,
				out_pos, false));
	}

	// Write out mode
	const char *mode;
	switch (options->mode){
	case LZMA_MODE_FAST:
		mode = LZMA_MODE_FAST_STR;
		break;
	case LZMA_MODE_NORMAL:
		mode = LZMA_MODE_NORMAL_STR;
		break;
	default:
		return LZMA_OPTIONS_ERROR;
	}

	return_if_error(write_out_str_option(out_str, LZMA_MODE_STR,
			mode, max_str_len, out_pos, false));
	// Write out nice_len
	return_if_error(write_out_num_option(out_str, LZMA_NICE_LEN_STR,
			options->nice_len, max_str_len, out_pos, false));

	// Write out mf
	const char* mf;
	switch (options->mf) {
	case LZMA_MF_HC3:
		mf = LZMA_MF_HC3_STR;
		break;
	case LZMA_MF_HC4:
		mf = LZMA_MF_HC4_STR;
		break;
	case LZMA_MF_BT2:
		mf = LZMA_MF_BT2_STR;
		break;
	case LZMA_MF_BT3:
		mf = LZMA_MF_BT3_STR;
		break;
	case LZMA_MF_BT4:
		mf = LZMA_MF_BT4_STR;
		break;
	default:
		return LZMA_OPTIONS_ERROR;
	}
	return_if_error(write_out_str_option(out_str, LZMA_MF_STR,
			mf, max_str_len, out_pos, false));
	// Write out depth
	// Putting the - 1 next to max_str_len allows us to skip the
	// length check for the delmimiter character
	return_if_error(write_out_num_option(out_str, LZMA_DEPTH_STR,
			options->depth, max_str_len - 1, out_pos, true));
	// Must write out delimiter even though LZMA filters must be the
	// last in the chain. lzma_filters_to_str does not validate
	// filter chains, it only converts them to strings
	out_str[(*out_pos)++] = LZMA_FILTER_DELIMITER;

	return LZMA_OK;
}


static lzma_ret
stringify_bcj_filter(lzma_filter *filter, char* out_str,
		size_t max_str_len, size_t *out_pos, const char* name)
{
	int name_len = strnlen(name, max_str_len);
	// The + 1 is because we will need at least 1 more character
	// for the delimiter
	if ((*out_pos + name_len + 1) > max_str_len) {
		return LZMA_BUF_ERROR;
	}
	memcpy(out_str + *out_pos, name, name_len);
	*out_pos += name_len;

	lzma_options_bcj *options = (lzma_options_bcj *) filter->options;
	if (options->start_offset > 0) {
		// Check for at least 3 characters for the = and at least
		// One digit for the start_offset
		// And one character for the delimiter
		if ((*out_pos + 3) > max_str_len) {
			return LZMA_BUF_ERROR;
		}
		out_str[(*out_pos)++] = LZMA_FILTER_OPTIONS_LIST_INDICATOR;
		return_if_error(write_out_num_option(out_str,
				LZMA_BCJ_START_OFFSET_STR,
				options->start_offset,
				max_str_len, out_pos, true));
	}

	// Write out delimiter character
	out_str[(*out_pos)++] = LZMA_FILTER_DELIMITER;

	return LZMA_OK;
}


static lzma_ret
stringify_delta_filter(lzma_filter *filter, char* out_str,
		size_t max_str_len, size_t *out_pos)
{
	int name_len = strnlen(LZMA_FILTER_DELTA_NAME, max_str_len);
	// The + 1 is because we will need at least 1 more character
	// for the delimiter
	if ((*out_pos + name_len + 1) > max_str_len) {
		return LZMA_BUF_ERROR;
	}
	memcpy(out_str + *out_pos, LZMA_FILTER_DELTA_NAME, name_len);
	*out_pos += name_len;

	lzma_options_delta *options = (lzma_options_delta *) filter->options;
	// Currently the only type for delta is LZMA_DELTA_TYPE_BYTE
	// so this will not be checked
	// The default for dist is LZMA_DELTA_DIST_MIN
	if (options->dist != LZMA_DELTA_DIST_MIN) {
		// Check for at least 3 characters for the = and at least
		// One digit for the start_offset
		// And one character for the delimiter
		if ((*out_pos + 3) > max_str_len) {
			return LZMA_BUF_ERROR;
		}
		out_str[(*out_pos)++] = LZMA_FILTER_OPTIONS_LIST_INDICATOR;
		return_if_error(write_out_num_option(out_str,
				LZMA_DELTA_DIST_STR, options->dist,
				max_str_len, out_pos, true));
	}

	// Write out delimiter character
	out_str[(*out_pos)++] = LZMA_FILTER_DELIMITER;

	return LZMA_OK;
}


static lzma_ret
parse_next_key(const char* str, size_t *in_pos, char* key)
{
	int i = 0;
	const char *substr = str + *in_pos;
	// Read into key until LZMA_FILTER_KEY_TO_VALUE_DELIMITER
	// is found or max length is read
	for (; i < MAX_OPTION_NAME_LEN; i++) {
		if (substr[i] == LZMA_FILTER_KEY_TO_VALUE_DELIMITER)
			break;
		key[i] = substr[i];
	}

	if (substr[i] != LZMA_FILTER_KEY_TO_VALUE_DELIMITER)
		return LZMA_PROG_ERROR;

	key[i] = 0;
	*in_pos += i+1;

	return LZMA_OK;
}


static lzma_ret
parse_next_value_str(const char* str, size_t *in_pos, char* value)
{
	int i = 0;
	const char *substr = str + *in_pos;
	// Read into value until
	// LZMA_FILTER_OPTION_DELIMITER, LZMA_FILTER_DELIMITER,
	// NULL, or max length is read
	for (; i < MAX_OPTION_NAME_LEN; i++) {
		if (substr[i] == LZMA_FILTER_OPTION_DELIMITER
				||substr[i] == LZMA_FILTER_DELIMITER
				|| substr[i] == 0) {
			break;
		}
		value[i] = substr[i];
	}

	*in_pos += i;
	value[i] = 0;

	if (substr[i] == LZMA_FILTER_OPTION_DELIMITER) {
		(*in_pos)++;
		return LZMA_OK;
	}
	else if (substr[i] == LZMA_FILTER_DELIMITER || substr[i] == 0) {
		return LZMA_STREAM_END;
	}
	else {
		return LZMA_PROG_ERROR;
	}
}


static lzma_ret
parse_next_value_uint32(const char* str, size_t *in_pos, uint32_t *value)
{
	uint32_t result = 0;
	const char* substr = str + *in_pos;
	int i = 0;
	char c = substr[i];
	while (c >= '0' && c <= '9') {
		// Check for overflow
		if (result > UINT32_MAX / 10)
			return LZMA_PROG_ERROR;

		result *= 10;

		// Check for overflow again
		uint32_t add = c - '0';
		if (UINT32_MAX - add < result)
			return LZMA_PROG_ERROR;
		// Add next digit
		result += add;
		c = substr[++i];
	}

	// Return if no suffix present
	// Do the suffix check here since most options will
	// not include the suffix so this will be an optimization
	if (c == LZMA_FILTER_OPTION_DELIMITER) {
		*in_pos += i + 1;
		*value = result;
		return LZMA_OK;
	}
	else if (c == LZMA_FILTER_DELIMITER || c == 0) {
		*in_pos += i;
		*value = result;
		return LZMA_STREAM_END;
	}

	uint32_t multiplier = 0;
	if (substr[i] == 'k' || substr[i] == 'K')
		multiplier = UINT32_C(1) << 10;
	else if (substr[i] == 'm' || substr[i] == 'M')
		multiplier = UINT32_C(1) << 20;
	else if (substr[i] == 'g' || substr[i] == 'G')
		multiplier = UINT32_C(1) << 30;
	else
		return LZMA_PROG_ERROR;

	i++;

	// Allow also e.g. Ki, KiB, and KB.
	if (!strcmp(&substr[i], "i") != 0 || !strcmp(&substr[i], "B") != 0)
		i++;
	else if (!strcmp(&substr[i], "iB") != 0)
		i += 2;

	// Don't overflow here either.
	if (result > UINT32_MAX / multiplier)
		return LZMA_PROG_ERROR;

	result *= multiplier;

	c = substr[i];
	if (c == LZMA_FILTER_OPTION_DELIMITER) {
		*in_pos += i + 1;
		*value = result;
		return LZMA_OK;
	}
	else if (c == LZMA_FILTER_DELIMITER || c == 0) {
		*in_pos += i;
		*value = result;
		return LZMA_STREAM_END;
	}
	else {
		return LZMA_PROG_ERROR;
	}
}

static lzma_ret
parse_lzma_filter(lzma_filter *filter, const lzma_allocator *allocator,
		const char* str, size_t *in_pos)
{
	// Read options one at a time until delimiter is found
	// or end of string.
	lzma_options_lzma *ops = (lzma_options_lzma *) lzma_alloc_zero(
			sizeof(lzma_options_lzma), allocator);
	if (ops == NULL)
		return LZMA_MEM_ERROR;
	filter->options = ops;
	if (str[*in_pos] == LZMA_FILTER_OPTIONS_LIST_INDICATOR) {
		// If the first character is a number 0-9 then use
		// it as a preset
		uint8_t digit = str[++(*in_pos)] - '0';
		if (digit < 10) {
			(*in_pos)++;
			if (lzma_lzma_preset(ops, digit))
				return LZMA_PROG_ERROR;
			else
				return LZMA_OK;
		}

		// Use the default preset first, then override with options
		// the user specifies
		if (lzma_lzma_preset(ops, LZMA_PRESET_DEFAULT))
			return LZMA_PROG_ERROR;

		// Parse key-value pairs until parse_next_value
		// returns an error or LZMA_STREAM_END
		char key[MAX_OPTION_NAME_LEN];
		char value[MAX_OPTION_VALUE_LEN];
		while (1) {
			return_if_error(parse_next_key(str, in_pos, key));
			lzma_ret ret = LZMA_OK;
			if (!strcmp(key, LZMA_DICT_SIZE_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->dict_size);
			}
			else if (!strcmp(key, LZMA_LC_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->lc);
			}
			else if (!strcmp(key, LZMA_LP_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->lp);
			}
			else if (!strcmp(key, LZMA_PB_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->pb);
			}
			else if (!strcmp(key, LZMA_MODE_STR)) {
				// Mode can be specified with the strings
				// LZMA_MODE_FAST_STR or LZMA_MODE_NORMAL_STR
				ret = parse_next_value_str(
						str, in_pos, value);
				if (!strcmp(value, LZMA_MODE_FAST_STR))
					ops->mode = LZMA_MODE_FAST;
				else if (!strcmp(value, LZMA_MODE_NORMAL_STR))
					ops->mode = LZMA_MODE_NORMAL;
				else
					return LZMA_PROG_ERROR;
			}
			else if (!strcmp(key, LZMA_NICE_LEN_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->nice_len);
			}
			else if (!strcmp(key, LZMA_MF_STR)) {
				ret = parse_next_value_str(
						str, in_pos, value);
				if (!strcmp(value, LZMA_MF_HC3_STR))
					ops->mf = LZMA_MF_HC3;
				else if (!strcmp(value, LZMA_MF_HC4_STR))
					ops->mf = LZMA_MF_HC4;
				else if (!strcmp(value, LZMA_MF_BT2_STR))
					ops->mf = LZMA_MF_BT2;
				else if (!strcmp(value, LZMA_MF_BT3_STR))
					ops->mf = LZMA_MF_BT3;
				else if (!strcmp(value, LZMA_MF_BT4_STR))
					ops->mf = LZMA_MF_BT4;
				else
					return LZMA_PROG_ERROR;
			}
			else if (!strcmp(key, LZMA_DEPTH_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->depth);
			}
			else {
				return LZMA_PROG_ERROR;
			}

			// The caller of the function will
			// return LZMA_STREAM_END if the value ended
			// with a NULL terminator
			if (ret == LZMA_STREAM_END)
				return LZMA_OK;
			else if (ret != LZMA_OK)
				return ret;
		}
	}
	// Otherwise, use default values for this filter and
	// do not advance the in_pos pointer
	else if (lzma_lzma_preset(ops, LZMA_PRESET_DEFAULT)) {
		return LZMA_PROG_ERROR;
	}

	return LZMA_OK;
}


static lzma_ret
parse_bcj_filter(lzma_filter *filter, const lzma_allocator *allocator,
		const char* str, size_t *in_pos)
{
	if (str[*in_pos] == LZMA_FILTER_OPTIONS_LIST_INDICATOR) {
		(*in_pos)++;
		lzma_options_bcj *ops = (lzma_options_bcj *) lzma_alloc_zero(
			sizeof(lzma_options_bcj), allocator);
		if (ops == NULL)
			return LZMA_MEM_ERROR;
		filter->options = ops;
		char key[MAX_OPTION_NAME_LEN];
		return_if_error(parse_next_key(str, in_pos, key));

		if (strcmp(key, LZMA_BCJ_START_OFFSET_STR))
			return LZMA_PROG_ERROR;

		lzma_ret ret = parse_next_value_uint32(str, in_pos,
				&ops->start_offset);
		if (ret != LZMA_OK && ret != LZMA_STREAM_END)
			return ret;
	}
	else {
		// Default BCJ filter has NULL options
		filter->options = NULL;
	}

	return LZMA_OK;
}


static lzma_ret
parse_delta_filter(lzma_filter *filter, const lzma_allocator *allocator,
		const char* str, size_t *in_pos)
{
	lzma_options_delta *ops = (lzma_options_delta *) lzma_alloc_zero(
			sizeof(lzma_options_delta), allocator);
	if (ops == NULL)
		return LZMA_MEM_ERROR;
	filter->options = ops;
	lzma_ret ret = LZMA_OK;
	if (str[*in_pos] == LZMA_FILTER_OPTIONS_LIST_INDICATOR) {
		(*in_pos)++;
		char key[MAX_OPTION_NAME_LEN];
		char value[MAX_OPTION_VALUE_LEN];

		while (1) {
			return_if_error(parse_next_key(str, in_pos, key));

			if (!strcmp(key, LZMA_DELTA_TYPE_STR)) {
				ret = parse_next_value_str(str, in_pos, value);

				if (!strcmp(value, LZMA_DELTA_TYPE_BYTE_STR)
						|| value[0] == '0') {
					ops->type = LZMA_DELTA_TYPE_BYTE;
				}
				else {
					return LZMA_PROG_ERROR;
				}
			}
			else if (!strcmp(key, LZMA_DELTA_DIST_STR)) {
				ret = parse_next_value_uint32(str, in_pos,
						&ops->dist);
			}
			else {
				return LZMA_PROG_ERROR;
			}

			if (ret == LZMA_STREAM_END)
				return LZMA_OK;
		}
	}
	else {
		// The default for Delta uses the dist as
		// LZMA_DELTA_DIST_MIN in xz,
		// so it is used the same here
		ops->type = LZMA_DELTA_TYPE_BYTE;
		ops->dist = LZMA_DELTA_DIST_MIN;
	}

	return ret;
}


static lzma_ret
parse_next_filter(lzma_filter *filter, const lzma_allocator *allocator,
		const char* str, size_t *in_pos)
{
	// First parse filter name from str
	char filter_name[FILTER_NAME_MAX_SIZE + 1];
	const char *substr = str + *in_pos;

	int i = 0;
	for (; i < FILTER_NAME_MAX_SIZE; i++) {
		if (substr[i] == LZMA_FILTER_DELIMITER ||
				substr[i] == LZMA_FILTER_OPTIONS_LIST_INDICATOR ||
				substr[i] == 0) {
			*in_pos += i;
			break;
		}
		filter_name[i] = substr[i];
	}

	// Null terminate filter_name
	filter_name[i] = 0;

	// Using filter name, determine which filter to create
	if (!strcmp(filter_name, LZMA_FILTER_LZMA1_NAME)) {
		filter->id = LZMA_FILTER_LZMA1;
		return_if_error(parse_lzma_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_LZMA2_NAME)) {
		filter->id = LZMA_FILTER_LZMA2;
		return_if_error(parse_lzma_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_X86_NAME)) {
		filter->id = LZMA_FILTER_X86;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_POWERPC_NAME)) {
		filter->id = LZMA_FILTER_POWERPC;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_IA64_NAME)) {
		filter->id = LZMA_FILTER_IA64;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_ARM_NAME)) {
		filter->id = LZMA_FILTER_ARM;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_ARMTHUMB_NAME)) {
		filter->id = LZMA_FILTER_ARMTHUMB;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_SPARC_NAME)) {
		filter->id = LZMA_FILTER_SPARC;
		return_if_error(parse_bcj_filter(filter, allocator,
				str, in_pos));
	}
	else if (!strcmp(filter_name, LZMA_FILTER_DELTA_NAME)) {
		filter->id = LZMA_FILTER_DELTA;
		return_if_error(parse_delta_filter(filter, allocator,
				str, in_pos));
	}
	else {
		// If we get here, filter name did not match
		return LZMA_PROG_ERROR;
	}

	// If str ends with the delimiter, then we need to advance
	// in_pos and return
	// If str ends with NULL terminator, we need to indicate
	// to the caller that no more filters should be read
	if (str[*in_pos] == LZMA_FILTER_DELIMITER) {
		(*in_pos)++;
		return LZMA_OK;
	}
	else if (str[*in_pos] == 0) {
		return LZMA_STREAM_END;
	}
	else {
		// Invalid end of filter detected
		return LZMA_PROG_ERROR;
	}
}


extern LZMA_API(lzma_ret) lzma_filters_to_str(
		const lzma_filter *filter, char* out_str, size_t max_str_len)
{
	// Sanity check for arguments
	if (filter == NULL || out_str == NULL)
		return LZMA_PROG_ERROR;

	size_t out_pos = 0;
	lzma_ret ret = LZMA_OK;

	for (int i = 0; filter[i].id != LZMA_VLI_UNKNOWN
			&& ret == LZMA_OK; i++) {
		if (i == LZMA_FILTERS_MAX)
			return LZMA_OPTIONS_ERROR;

		switch (filter[i].id) {
		case LZMA_FILTER_LZMA1:
			ret = stringify_lzma_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_LZMA1_NAME);
			break;
		case LZMA_FILTER_LZMA2:
			ret = stringify_lzma_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_LZMA2_NAME);
			break;
		case LZMA_FILTER_X86:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_X86_NAME);
			break;
		case LZMA_FILTER_POWERPC:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_POWERPC_NAME);
			break;
		case LZMA_FILTER_IA64:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_IA64_NAME);
			break;
		case LZMA_FILTER_ARM:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_ARM_NAME);
			break;
		case LZMA_FILTER_ARMTHUMB:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_ARMTHUMB_NAME);
			break;
		case LZMA_FILTER_SPARC:
			ret = stringify_bcj_filter(&filter[i], out_str,
					max_str_len, &out_pos,
					LZMA_FILTER_SPARC_NAME);
			break;
		case LZMA_FILTER_DELTA:
			ret = stringify_delta_filter(&filter[i], out_str,
					max_str_len, &out_pos);
			break;
		default:
			return LZMA_OPTIONS_ERROR;
		}
	}

	// NULL terminate result over the last
	// LZMA_FILTER_DELIMITER character
	if (ret == LZMA_OK) {
		if (out_pos <= max_str_len && out_pos > 0)
			out_str[out_pos - 1] = 0;
		else
			return LZMA_BUF_ERROR;
	}

	return ret;
}


extern LZMA_API(lzma_ret) lzma_str_to_filters(
		lzma_filter *filter, const lzma_allocator *allocator,
		const char* str)
{
	// NULL check arguments
	if (filter == NULL || str == NULL)
		return LZMA_PROG_ERROR;

	size_t in_pos = 0;

	int i = 0;
	for (; i < LZMA_FILTERS_MAX; i++) {
		filter[i].id = LZMA_VLI_UNKNOWN;
		filter[i].options = NULL;
		lzma_ret ret = parse_next_filter(&filter[i], allocator, str,
				&in_pos);
		if (ret != LZMA_STREAM_END && ret != LZMA_OK){
			// Free any allocated options after error
			for (int j = 0; j <= i; j++) {
				if (filter[j].options != NULL){
					lzma_free(filter[j].options,
							allocator);
				}

			}
			return ret;
		}
		else if (ret == LZMA_STREAM_END || str[in_pos] == 0){
			break;
		}
	}

	i++;
	filter[i].id = LZMA_VLI_UNKNOWN;
	filter[i].options = NULL;

	return LZMA_OK;
}
