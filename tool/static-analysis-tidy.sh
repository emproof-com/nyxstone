#!/bin/bash
set -eo pipefail

# Ensure that clang-tidy is installed
if ! command -v clang-tidy &> /dev/null
then
    echo "Failed to find clang-tidy"
    echo "Are you sure it is installed?"
    exit 1
fi

# Since we cannot be sure from which directory we are called from, try to find 
# nyxstone and cd into it.
cd "$(git rev-parse --show-toplevel)"

# Search all nyxstone cpp files but ignore the files copied from LLVM by only searching
# to a depth of 2.
cxx_files=$(find . -maxdepth 2 -iname "*.cpp" | xargs echo)
# Include directories so that headers can be found.
includes="-Iinclude -Ivendor"

# Set standard to c++17.
cxx_flags="-std=c++17"
# Tell tidy to use libc++, as it is otherwise is unable to find any function the stdlib.
cxx_flags+=" -stdlib=libc++"

# Add flags needed for llvm.
if [[ "$NYXSTONE_LLVM_PREFIX" != "" ]]; then
    llvm="$NYXSTONE_LLVM_PREFIX/bin/llvm-config"
else
    llvm="llvm-config"
fi
cxx_flags+=" $("$llvm" --cppflags)"

# We are currently not analyzing the ffi files, i.e. nyxstone_wrap
# because there is no nice way to include the cxx.h file from the rustcxx
# package. Ignoring it is not possible, since clang-tidy handles this as a
# compiler error.
# include ffi nyxstone_wrap header:
# includes+=" -Isrc"
# check nyxstone_wrap
# cxx_files+=" ./src/nyxstone_wrap.cpp"

# For some reason clang-tidy fails when the args string is given directly,
# which is the reason for writing them to this variable first:
clang_args="$cxx_files -- $cxx_flags $includes"
clang-tidy $clang_args
