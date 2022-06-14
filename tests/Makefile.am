##
## Author: Lasse Collin
##
## This file has been put into the public domain.
## You can do whatever you want with this file.
##

stest_SOURCES = \
	stest/stest.c \
	stest/stest.h

EXTRA_DIST = \
	files \
	ossfuzz \
	tuktest.h \
	tests.h \
	test_files.sh \
	test_compress.sh \
	test_compress_prepared_bcj_sparc \
	test_compress_prepared_bcj_x86 \
	test_compress_generated_abc \
	test_compress_generated_random \
	test_compress_generated_text \
	test_scripts.sh \
	bcj_test.c \
	compress_prepared_bcj_sparc \
	compress_prepared_bcj_x86 \
	test_scripts.sh \
	$(stest_SOURCES)

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/liblzma/api \
	-I$(top_builddir)/lib \
	-I$(top_srcdir)/tests/stest \
	-DROOT_DIR=$(abs_top_srcdir)

LDADD = $(top_builddir)/src/liblzma/liblzma.la

if COND_GNULIB
LDADD += $(top_builddir)/lib/libgnu.a
endif

LDADD += $(LTLIBINTL)

liblzma_tests_SOURCES = \
	$(stest_SOURCES) \
	test_check.c \
	test_block_header.c \
	test_bcj_exact_size.c \
	test_filter_flags.c \
	test_index.c \
	test_stream_flags.c \
	test_lzma_filter_utils.c \
	test_lzma_filters.c \
	test_lzma_raw.c \
	test_lzma_properties.c \
	test_utils.c \
	test_lzma_filter_str_conversion.c \
	liblzma_tests.c

xz_tests_SOURCES = \
	$(stest_SOURCES) \
	test_xz_decompress.c \
	test_xz_compress.c \
	test_utils.c \
	xz_tests.c

check_PROGRAMS = \
	liblzma_tests \
	xz_tests

TESTS = \
	test_check \
	test_stream_flags \
	test_filter_flags \
	test_block_header \
	test_index \
	test_bcj_exact_size \
	test_files.sh \
	test_compress_prepared_bcj_sparc \
	test_compress_prepared_bcj_x86 \
	test_compress_generated_abc \
	test_compress_generated_random \
	test_compress_generated_text

if COND_SCRIPTS
TESTS += test_scripts.sh
endif

clean-local:
	-rm -f compress_generated_* \
		xzgrep_test_output xzgrep_test_1.xz xzgrep_test_2.xz
