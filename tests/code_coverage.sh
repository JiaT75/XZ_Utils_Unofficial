#!/bin/sh

###############################################################################
#
# Author: Jia Tan
#
# This file has been put into the public domain.
# You can do whatever you want with this file.
#
###############################################################################

set -e

# If root directory is not specified, this file must be run from the
# tests directory
rootdir=..
builddir=coverage_build

for i in "$@"; do
  case $i in
    -r=*|--rootdir=*)
      rootdir="${i#*=}"
      shift
      ;;
    -b=*|--builddir=*)
      builddir="${i#*=}"
      shift
      ;;
    -h|--help)
      echo "Usage: code_coverage.sh [OPTION]
Helper script to create code coverage reports using lcov and genhtml\n
Options:
	-r, --rootdir=[PATH]    Path to the root directory of the project
				(Default value is ..)
	-b, --builddir=[PATH]   Path to the build directory from the rootdir
	                        for the code coverage build.
				(Default value is coverage_build)
	-h, --help              Display help message"
      exit 0
      shift
      ;;
    -*|--*)
      echo "Unknown option $i"
      exit 1
      ;;
    *)
      ;;
  esac
done

# Test if lcov is installed
if ! command -v lcov > /dev/null
then
	echo "Error: lcov not installed"
	exit 1
fi

# Test is genhtml is installed
if ! command -v genhtml > /dev/null
then
	echo "Error: genhtml not installed"
	exit 1
fi

cd "$rootdir"

# Run autogen script if configure script has not been generated
if ! test -f "configure"
then
	./autogen.sh
fi

# Reconfigure the project with --enable-cov
rm -rf "$builddir"
mkdir -p "$builddir"
cd "$builddir"
"./$rootdir/configure"

# Run the tests
make check CFLAGS="$CFLAGS --coverage --no-inline -O0"

# Re-create the coverage directory
mkdir -p "$rootdir/tests/coverage/liblzma"
mkdir -p "$rootdir/tests/coverage/xz"

# Run lcov with src/liblzma/.libs as the input directory and write the
# results out to coverage
lcov -c -d "src/liblzma/.libs" -o "$rootdir/tests/coverage/liblzma/liblzma.cov"
lcov -c -d "src/xz/" -o "$rootdir/tests/coverage/xz/xz.cov"

# Generate the reports
genhtml "$rootdir/tests/coverage/liblzma/liblzma.cov" -o "$rootdir/tests/coverage/liblzma"
genhtml "$rootdir/tests/coverage/xz/xz.cov" -o "$rootdir/tests/coverage/xz"

# Clean up build directory
cd "$rootdir"
rm -rf "$builddir"

echo "Success! Output generated to tests/coverage/liblzma/index.html and"\
	"tests/coverage/xz/index.html"
