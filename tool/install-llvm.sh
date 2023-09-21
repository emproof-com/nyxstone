#!/bin/bash

# fail on error
set -eo pipefail

pwd="$PWD"

installdir="$1"
confirm=1
if [[ "$installdir" = "-y" ]]; then
  installdir="$2"
  confirm=0
fi

if [[ -e $installdir && ! -d $installdir ]]; then
  echo "Usage: $0 (-y) [INSTALLATION DIRECTORY]"
  exit 1
fi

if [[ ! -d $installdir ]]; then
  echo "Creating installation directory."
  mkdir $installdir
fi

echo "This script expects 'curl', 'cmake', 'ninja', 'clang' and 'tar' to be installed."

installdir="$(realpath $installdir)"

if [[ $confirm = 1 ]]; then
  read -p "Please supply the full llvm version that should be installed for nyxstone [15.0.7]: " version
  version=${version:-"15.0.7"}
else
  version="15.0.7"
fi

dir="llvm$version"

tmpdir="/tmp/llvm$version"
archive="/tmp/$dir.tar.gz"

if [[ ! -e $archive && ! -d $tmpdir ]]; then
  # First we need the llvm source code so that we can compile it from source
  echo "# Downloading $archive."
  status=$(curl -sSL -w '%{http_code}' -o "$archive" https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-$version.tar.gz)

  if [[ ! "$status" =~ ^2 ]]; then
    rm "$archive"
    echo "Error: Expected 2xx http response code, got $status."
    exit 1
  fi
else
  echo "# Found $archive or $tmpdir."
fi

if [[ ! -e $tmpdir ]]; then
  mkdir $tmpdir
  echo "# Unpacking $archive into $tmpdir"
  tar -xf "$archive" -C $tmpdir --strip-components 1
else
  echo "# Found $tmpdir, assuming llvm has already been unpacked"
fi

cd $tmpdir

mkdir -p build/
cd build/

if [[ $confirm = 1 ]]; then
  echo "Please supply the targets that should be supported as semi-colon seperated values."
  echo "Possible targets: AArch64, AMDGPU, ARM, AVR, BPF, Hexagon, Lanai, Mips, MSP430, NVPTX, PowerPC, RISCV, Sparc, SystemZ, WebAssembly, X86, XCore."
  echo "Your llvm version might support other targets than shown here, please see the llmv docs for your version."
  read -p "Targets [AArch64;ARM;X86;RISCV]: " targets
fi
targets=${targets:-"AArch64;ARM;X86;RISCV"}

# Use Ninja since its the fastest build system for llvm.
# Note: LLVM builds are static by default.
env CC=clang CXX=clang++ cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$installdir" \
  -DLLVM_TARGETS_TO_BUILD="$targets" \
  -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_LIBCXX=OFF \
  -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF \
  -DLLVM_LINK_LLVM_DYLIB=OFF \
  -DLLVM_ENABLE_EH=ON \
  -DLLVM_ENABLE_FFI=ON \
  -DLLVM_ENABLE_RTTI=ON \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INSTALL_UTILS=OFF \
  -DLLVM_ENABLE_BINDINGS=OFF \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBPFM=OFF \
  -DLLVM_ENABLE_Z3_SOLVER=OFF \
  -DLLVM_OPTIMIZED_TABLEGEN=ON \
  -G Ninja \
  ../llvm

cmake --build . --target install --config Release

echo "# Cleaning up $archive"
rm $archive

echo "# Cleaning up $tmpdir"
rm -r "$tmpdir"
