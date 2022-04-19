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
	liblzma_tests.h \
	xz_tests.h \
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
	liblzma_tests \
	xz_tests

if COND_SCRIPTS
TESTS += test_scripts.sh
endif

clean-local:
	-rm -f compress_generated_* \
		xzgrep_test_output xzgrep_test_1.xz xzgrep_test_2.xz
