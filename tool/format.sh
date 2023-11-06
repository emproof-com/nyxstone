#!/bin/bash
set -eo pipefail

# Ensure that clang-format is installed
if ! command -v clang-format &> /dev/null
then
    echo "Failed to find clang-format"
    echo "Are you sure it is installed?"
    exit 1
fi

# Since we cannot be sure from which directory we are called from, try to find 
# nyxstone and cd into it.
cd "$(git rev-parse --show-toplevel)"

# Ignore rust auto-generated c++ files and tl/expected.hpp
files=$(find . ! -wholename "*target*" ! -wholename "*build*" ! -wholename "tl/expected.hpp" \( -iname "*.cpp" -o -iname "*.hpp" -o -iname "*.h" \))
if [[ "$1" == "check" ]]; then
    echo "$files" | xargs clang-format --dry-run -Werror
elif [ ! -z "$1" ]; then
    echo "Usage: $0 [check]"
    echo "Without [check] formats all c/c++ files in place."
    echo "With [check], checks all files, exits with error code if files are not formatted."
else
    echo "$files" | xargs clang-format -i
fi
