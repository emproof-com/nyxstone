# Nyxstone

[![Github Cpp CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml)
[![crates.io](https://img.shields.io/crates/v/nyxstone.svg)](https://crates.io/crates/nyxstone)
[![PyPI](https://img.shields.io/pypi/v/nyxstone.svg)](https://pypi.org/project/nyxstone)
[![cpp-docs](https://github.com/emproof-com/nyxstone/actions/workflows/doxygen.yml/badge.svg)](https://emproof-com.github.io/nyxstone/)

Nyxstone is a fast assembly and disassembly library built on top of LLVM. It does not require patches to the LLVM source tree and links against the standard LLVM libraries shipped by most Linux distributions, Homebrew, and `apt.llvm.org`. The core is a C++ library with Rust and Python bindings. Nyxstone supports every architecture the linked LLVM ships with and lets you configure architecture-specific CPU and feature settings.

![Nyxstone Python binding demo](/images/demo.svg)

## Index

1. [Core Features](#core-features)
2. [Using Nyxstone](#using-nyxstone)
    1. [Prerequisites](#prerequisites)
    2. [CLI Tool](#cli-tool)
    3. [C++ Library](#c-library)
    4. [Rust Bindings](#rust-bindings)
    5. [Python Bindings](#python-bindings)
3. [How it works](#how-it-works)
4. [Benchmarks](#benchmarks)
5. [Roadmap](#roadmap)
6. [License](#license)
7. [Contributing](#contributing)
8. [Maintainers](#maintainers)

## Core Features

* Assembles and disassembles code for every architecture supported by the linked LLVM, including x86, ARM, AArch64, MIPS, RISC-V, and others.

* C++ library built on LLVM, with Rust and Python bindings.

* Native platform support for Linux and macOS.

* Supports labels in the assembler, including user-provided label-to-address mappings.

* Produces raw bytes, text disassembly, or detailed instruction objects that carry the address, raw bytes, and assembly text together.

* Disassembly can be limited to a user-specified number of instructions.

* Configurable per-architecture target settings (CPU, ISA extensions, hardware features).

* Assembles common data directives (`.byte`, `.word`, `.org`, `.nops`, `.align`, `.fill`, `.uleb128`, …) and the ARM/AArch64 `ldr rX, =const` literal pool.

* Reports an explicit error for input it cannot represent (for example, switching to a section other than `.text`) instead of silently dropping the affected bytes.

For the list of supported architectures, run `clang -print-targets`. For per-architecture features, run `llc -march=ARCH -mattr=help`.

> [!NOTE]
> Nyxstone has been primarily developed and tested on x86_64, AArch64, and ARM32. We have high confidence in its correctness and error reporting for those targets. For other architectures, the result depends on the underlying LLVM backend.

## Using Nyxstone

This section provides instructions on how to get started with Nyxstone, covering the necessary prerequisites, how to use the CLI tool, and step-by-step guidelines for using the library with C++, Rust, and Python.

### Prerequisites

Before building Nyxstone, ensure clang and LLVM are present on your system. **Nyxstone supports LLVM major versions 15-20.** Any minor/patch within those majors works; the build picks the newest LLVM it can find unless you pin one.

The build resolves LLVM in this order:

1. `$NYXSTONE_LLVM_PREFIX`, if set. The build searches that prefix exclusively (system paths are ignored), so this is the way to pin a specific version when multiple are installed.
2. Known per-major install layouts probed newest-first: `/usr/lib/llvm-<N>` (Debian/Ubuntu), `/opt/homebrew/opt/llvm@<N>` (Homebrew on Apple Silicon), `/usr/local/opt/llvm@<N>` (Homebrew on x86 macOS), `/opt/brew/opt/llvm@<N>` (custom-prefix Homebrew on Linux).
3. CMake's default `find_package(LLVM)` search.

If the resolved version is outside 15-20 the configure step fails with a clear error.

#### Installation

* **Debian / Ubuntu**
  ```bash
  sudo apt install llvm-${version} llvm-${version}-dev
  ```
  Debian trixie ships 17-19, Ubuntu ships 15-17 in the default repos. For versions not in your distro's repos, follow the instructions at [apt.llvm.org](https://apt.llvm.org/). The script `apt.llvm.org/llvm.sh <version>` is the easiest path.

* **Arch**
  ```bash
  sudo pacman -S llvm llvm-libs
  ```

* **Homebrew (macOS / Linux)**
  ```bash
  brew install llvm@20
  export NYXSTONE_LLVM_PREFIX="$(brew --prefix llvm@20)"
  ```

* **From source**

  On Windows, run these from a Visual Studio 2022 x64 command prompt and replace `~/lib/my-llvm-20` with a path of your choice.

  ```bash
  git clone -b release/20.x --single-branch https://github.com/llvm/llvm-project.git
  cd llvm-project
  cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_PARALLEL_LINK_JOBS=1
  cmake --build build
  cmake --install build --prefix ~/lib/my-llvm-20
  export NYXSTONE_LLVM_PREFIX=~/lib/my-llvm-20
  ```

You may also need any system libraries LLVM was built against. Check with `llvm-config --system-libs`; on Debian/Ubuntu this is typically `zlib1g-dev` and `libzstd-dev`.

### CLI Tool

Nyxstone ships a [CLI tool](examples/nyxstone-cli.cpp) for one-off assembly and disassembly. Clone the repository and build it with CMake:

```bash
git clone https://github.com/emproof-com/nyxstone
cd nyxstone
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The resulting `nyxstone` binary lands in `build/`. Its help menu:

```
$ ./nyxstone -h
Usage: nyxstone [-t=<triple>] [-p=<pc>] [-d] <input>

Examples:
  # Assemble an instruction with the default architecture ('x86_64').
  nyxstone 'push eax'

  # Disassemble the bytes 'ffc300d1' as AArch64 code.
  nyxstone -t aarch64 -d ffc300d1

Options:
  -t, --triple=<triple>      LLVM target triple or alias, e.g. 'aarch64'
  -c, --cpu=<cpu>            LLVM CPU specifier, e.g. 'cortex-a53'
  -f, --features=<list>      LLVM architecture/CPU feature list, e.g. '+mte,-neon'
  -p, --address=<pc>         Initial address to assemble/disassemble relative to
  -l, --labels=<list>        Label-to-address mappings (used when assembling only)
  -d, --disassemble          Treat <input> as bytes to disassemble instead of assembly
  -h, --help                 Show this help and usage message

Notes:
  The '--triple' parameter also supports aliases for common target triples:

     'x86_32' -> 'i686-linux-gnu'
     'x86_64' -> 'x86_64-linux-gnu'
     'armv6m' -> 'armv6m-none-eabi'
     'armv7m' -> 'armv7m-none-eabi'
     'armv8m' -> 'armv8m.main-none-eabi'
    'aarch64' -> 'aarch64-linux-gnueabihf'

  The CPUs for a target can be found with 'llc -mtriple=<triple> -mcpu=help'.
  The features for a target can be found with 'llc -mtriple=<triple> -mattr=help'.
```

Assemble a single x86_64 instruction:

```
$ ./nyxstone -t x86_64 "mov rax, rbx"
        0x00000000: mov rax, rbx                     ; 48 89 d8
```

Assemble a sequence with internal labels, anchored at `0xdeadbeef`:

```
$ ./nyxstone -t x86_64 -p 0xdeadbeef "cmp rax, rbx; jz .exit; inc rax; .exit: ret"
        0xdeadbeef: cmp rax, rbx                     ; 48 39 d8 
        0xdeadbef2: je .exit                         ; 74 03 
        0xdeadbef4: inc rax                          ; 48 ff c0 
        0xdeadbef7: ret                              ; c3 
```

Disassemble for a different ISA, here ARM Thumb:

```
$ ./nyxstone -t thumbv8 -d "13 37"
        0x00000000: adds r7, #19                     ; 13 37 
```

Pin an external label to a specific address with `--labels`:

```
$ ./nyxstone -p "0x1000" -l ".label=0x1238" "jmp .label"
        0x00001000: jmp .label                       ; e9 33 02 00 00
```

### C++ Library

Link your code against the `nyxstone::nyxstone` CMake target. Nyxstone propagates its LLVM dependency transitively, so consumers do not need to call `find_package(LLVM)` themselves.

Assuming Nyxstone lives in a `nyxstone/` subdirectory:

```cmake
add_subdirectory(nyxstone)

add_executable(my_executable main.cpp)
target_link_libraries(my_executable PRIVATE nyxstone::nyxstone)
```

A minimal C++ usage example:

```c++
#include <cassert>
#include <vector>

#include "nyxstone.h"

using namespace nyxstone;

int main() {
    auto nyxstone = NyxstoneBuilder("x86_64").build().value();

    // Assemble to bytes.
    auto bytes = nyxstone->assemble(/*assembly=*/"mov rax, rbx",
                                    /*address=*/0x1000,
                                    /*labels=*/{}).value();

    const std::vector<uint8_t> expected{0x48, 0x89, 0xd8};
    assert(bytes == expected);
}
```

For a fuller walkthrough, including `assemble_to_instructions`, label definitions, and disassembly, see [examples/example.cpp](examples/example.cpp).


### Rust Bindings

Add `nyxstone` to your `Cargo.toml` and use it like so:

```rust
use anyhow::Result;
use nyxstone::{Nyxstone, NyxstoneConfig};

use std::collections::HashMap;

fn main() -> Result<()> {
    let nyxstone = Nyxstone::new("x86_64", NyxstoneConfig::default())?;

    let bytes = nyxstone.assemble_with(
        "mov rax, rbx; cmp rax, rdx; jne .label",
        0x1000,
        &HashMap::from([(".label", 0x1200)]),
    )?;

    println!("Bytes: {:x?}", bytes);

    Ok(())
}
```

See the [Rust binding README](bindings/rust/README.md) for build options (static vs. dynamic LLVM linking, FFI quirks).

### Python Bindings

Install via pip:

```bash
pip install nyxstone
```

Then from Python:

```
$ python -q
>>> from nyxstone import Nyxstone
>>> nyxstone = Nyxstone("x86_64")
>>> nyxstone.assemble("jne .loop", 0x1100, {".loop": 0x1000})
```

See the [Python binding README](bindings/python/README.md) for build-from-source instructions.



## How it works

Nyxstone drives the LLVM MC layer through its public C++ API. The assembler runs input through LLVM's regular `MCELFStreamer` object-emission pipeline, so LLVM performs layout, relaxation, and relocation resolution itself. That is what keeps target-specific relocations correct across every backend — for example RISC-V's paired `%pcrel_hi`/`%pcrel_lo` behind the `la` pseudo instruction. Nyxstone then extracts the `.text` bytes and applies a few targeted post-processing steps.

The assembly path is structured as follows:

* **`.text`-only setup.** Nyxstone installs a minimal `TextOnlyObjectFileInfo` that registers only `.text` with the `MCContext`, skipping the ~40 other section creations `MCObjectFileInfo::initMCObjectFileInfo` performs by default. This is the largest single performance win over a full object-file setup.

* **Stream through LLVM.** A thin `MCELFStreamer` subclass, [`ELFStreamerWrapper`](src/ELFStreamerWrapper.h), records each instruction's assembly text for the instruction-detail API and rejects switching to any section other than `.text`, so unsupported directives raise an explicit error instead of silently dropping the bytes that follow. Common data directives (`.byte`/`.word`/`.org`/`.nops`/`.align`/`.fill`/`.uleb128`/…) and the `ldr rX, =const` literal pool work because they flow through the normal pipeline.

* **Resolve address-sensitive fixups.** LLVM lays the section out at base 0, so after layout Nyxstone re-applies the one relocation LLVM defers to link time and that depends on the runtime base — AArch64 `adrp` (page-of-target minus page-of-pc) — against the user-supplied address, then extracts `.text` with `MCAssembler::writeSectionData`.

* **Handle ARM Thumb alignment.** When the start address is 2- but not 4-byte aligned, LLVM's base-0 layout has the wrong alignment parity and would mis-relax or reject Thumb PC-relative loads. Nyxstone prepends an internal 2-byte `bkpt` to restore parity and strips it from the output.

* **Validate.** Nyxstone runs additional range and alignment checks for ARM Thumb (`adr`, `ldr` literal, `b/bl/bcc`, …) and AArch64 (`adr`) fixup kinds that LLVM's backend silently mis-encodes when out of range.

The disassembly path is much simpler: an `MCDisassembler` and its `MCContext` are constructed once on each `Nyxstone` instance and reused across calls, since disassembly never mutates the context.

* **Caching.** The version-independent target-info objects (`MCRegisterInfo`, `MCInstrInfo`, `MCSubtargetInfo`, `MCAsmInfo`), the instruction printer, and the `MCAsmBackend` are built once per `Nyxstone` instance and reused. The assembler's `MCContext` and the per-call streamer/parser are rebuilt on each call, because LLVM ties the context to the input source buffer.

* **Version coupling.** Nyxstone uses MC headers that LLVM does not promise stable across major versions; the supported range (15-20) is covered by `#if LLVM_VERSION_MAJOR` guards in [src/nyxstone.cpp](src/nyxstone.cpp) and [src/ELFStreamerWrapper.h](src/ELFStreamerWrapper.h) — most notably the removal of `MCAsmLayout` in LLVM 19. The vendored LLVM-internal headers under [src/Target/](src/Target/) (`AArch64FixupKinds.h`, `AArch64MCExpr.h`, `ARMFixupKinds.h`) are tracked similarly, because LLVM does not install them.

## Benchmarks

Numbers below were collected with the bundled benchmark binaries on a 13th Gen Intel Core i7-1370P (Linux, LLVM 19, release build, 2 s per measurement). Reproduce with:

```bash
# C++
cmake --build build --target benchmark
./build/benchmark 2

# Rust
cargo run --release --example benchmark -- 2
```

Each call assembles/disassembles a package of 1 or 10 instructions for the named architecture. The 10-instruction package amortizes fixed per-call overhead, which is why `ops/s` drops but `insns/s` (parenthesized) rises.

| Architecture | Package      | C++ assemble (ops/s) | C++ disassemble (ops/s) |
| ------------ | ------------ | -------------------- | ----------------------- |
| x86_64       | 1 instr      | 68 k                 | 6.34 M                  |
| x86_64       | 10 instr     | 42 k (425 k insns/s) | 671 k (6.71 M insns/s)  |
| x86_32       | 1 instr      | 69 k                 | 6.29 M                  |
| x86_32       | 10 instr     | 45 k (448 k insns/s) | 685 k (6.85 M insns/s)  |
| aarch64      | 1 instr      | 24 k                 | 4.54 M                  |
| aarch64      | 10 instr     | 6.9 k (69 k insns/s) | 376 k (3.76 M insns/s)  |
| armv8m       | 1 instr      | 60 k                 | 5.64 M                  |
| armv8m       | 10 instr     | 29 k (285 k insns/s) | 392 k (3.92 M insns/s)  |

The Rust binding adds the cxx-bridge call overhead. For assembly this is negligible (within ~3 % of the C++ numbers); for the much faster disassembly path it costs roughly 30 % on single-instruction calls and shrinks to near-zero as the call does more work.

## Roadmap

Recent work:

* [x] Drive assembly through LLVM's `MCELFStreamer` pipeline so LLVM resolves relocations for every target, including RISC-V `%pcrel_hi`/`%pcrel_lo` (the `la` pseudo) that an earlier hand-rolled fixup pass got wrong.
* [x] Keep assembly fast with a `.text`-only `MCObjectFileInfo` and by caching the target-info objects, instruction printer, `MCAsmBackend`, and `MCDisassembler` across calls.
* [x] Support common data directives (`.byte`/`.word`/`.org`/`.nops`/`.align`/`.fill`/`.uleb128`/…) and the `ldr =const` literal pool.
* [x] Raise explicit errors on input Nyxstone cannot represent (e.g. switching away from `.text`) instead of silently dropping bytes.
* [x] Resolve the relocations LLVM defers to link time (AArch64 `adrp`) and run range/alignment validators for ARM Thumb / AArch64 fixup kinds LLVM mis-encodes.
* [x] Support LLVM 15-20 with auto-selection of the newest installed version.

Still open:

* [ ] Verify and document thread safety beyond `NyxstoneBuilder::build()` (LLVM init is mutex-guarded; subsequent `assemble`/`disassemble` are not formally verified concurrent-safe on a single instance).
* [ ] Extend support to LLVM 21+ (each new major tends to shift the unstable MC headers Nyxstone depends on; LLVM 19 already required handling the removal of `MCAsmLayout`).

## License

Nyxstone is available under the [MIT license](LICENSE).

## Contributing

Contributions are welcome. If you hit a bug, please open a GitHub issue with a reproducer (target triple, input, expected vs. actual output).

If you'd like to send code, useful starting points are:

* Picking up an open roadmap item or an existing issue.
* Adding tests, particularly for architectures other than x86/AArch64/ARM, where coverage is thinnest.
* Improving the documentation.

PRs welcome, please run the existing C++ and Rust tests, plus `tool/format.sh check` and `tool/static-analysis-{cppcheck,tidy}.sh`, before opening one.

## Maintainers

Nyxstone is maintained by [emproof](https://emproof.com). For questions or anything that needs a real human, ping:

* [Philipp Koppe](https://github.com/pkoppe)
* [Darius Hartlief](https://github.com/stuxnot)

For the full list of everyone who has contributed code, see [the contributors graph](https://github.com/emproof-com/nyxstone/graphs/contributors).

## Acknowledgements

To ensure that we link LLVM correctly with proper versioning in Rust, we adapted the build.rs from [llvm-sys](https://gitlab.com/taricorp/llvm-sys.rs/).
