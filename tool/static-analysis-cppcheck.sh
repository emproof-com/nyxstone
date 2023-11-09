#!/bin/bash
set -eo pipefail

# Ensure that cppcheck is installed
if ! command -v cppcheck &> /dev/null
then
    echo "Failed to find cppcheck"
    echo "Are you sure it is installed?"
    exit 1
fi

# Since we cannot be sure from which directory we are called from, try to find 
# nyxstone and cd into it.
cd "$(git rev-parse --show-toplevel)"

# we only search for files to a depth of two so that 
# the llvm files in `Target` are ingnored, since they are
# copied directly from llvm.
cxx_files=$(find . -maxdepth 2 -iname "*.cpp" | xargs echo)
includes="-Iinclude -Isrc"

cppcheck --enable=all --suppress=*:include/tl/expected.hpp --inline-suppr --error-exitcode=1 --language=c++ --suppress=missingIncludeSystem $cxx_files $cxx_ffi_files $includes
