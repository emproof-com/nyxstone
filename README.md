# Nyxstone

[![Github Cpp CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml)
[![crates.io](https://img.shields.io/crates/v/nyxstone.svg)](https://crates.io/crates/nyxstone)
[![PyPI](https://img.shields.io/pypi/v/nyxstone.svg)](https://pypi.org/project/nyxstone)

Nyxstone is a powerful assembly and disassembly library based on LLVM. It doesn’t require patches to the LLVM source tree and links against standard LLVM libraries available in most Linux distributions. Implemented as a C++ library, Nyxstone also offers Rust and Python bindings. It supports all official LLVM architectures and allows to configure architecture-specific target settings.

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
4. [Roadmap](#roadmap)
5. [License](#license)
6. [Contributing](#contributing)
7. [Contributors](#contributors)

## Core Features

* Assembles and disassembles code for all architectures supported by LLVM 15, including x86, ARM, MIPS, RISC-V and others.

* C++ library based on LLVM with Rust and Python bindings.

* Native platform support for Linux and macOS.

* Supports labels in the assembler, including the specification of label-to-address mappings

* Assembles and disassembles to raw bytes and text, but also provides detailed instruction objects containing the address, raw bytes, and the assembly representation.

* Disassembly can be limited to a user-specified number of instructions from byte sequences.

* Allows to configure architecture-specific target features, such as ISA extensions and hardware features.

For a comprehensive list of supported architectures, you can use `clang -print-targets`. For a comprehensive list of features for each architecture, refer to `llc -march=ARCH -mattr=help`.

> [!NOTE]
> Disclaimer: Nyxstone has been primarily developed and tested for x86_64, AArch64, and ARM32 architectures. We have a high degree of confidence in its ability to accurately generate assembly and identify errors for these platforms. For other architectures, Nyxstone's effectiveness depends on the reliability and performance of their respective LLVM backends.

## Using Nyxstone

This section provides instructions on how to get started with Nyxstone, covering the necessary prerequisites, how to use the CLI tool, and step-by-step guidelines for using the library with C++, Rust, and Python.

### Prerequisites

Before building Nyxstone, ensure clang and LLVM 18 are present on your system. Nyxstone looks for `llvm-config` in your system's `$PATH` or the specified environment variable `$NYXSTONE_LLVM_PREFIX/bin`.

Installation Options for LLVM 18:

* Ubuntu
```bash
sudo apt install llvm-18 llvm-18-dev
```

* Debian
LLVM 18 is currently only available via the testing repositories.
Refer to [https://apt.llvm.org/](https://apt.llvm.org/) for install instructions.

* Arch
```bash
sudo pacman -S llvm llvm-libs
```

* Homebrew (macOS, Linux and others):
```bash
brew install llvm@18
export NYXSTONE_LLVM_PREFIX=/opt/homebrew/opt/llvm@18
```

* From LLVM Source:

_Note_: On Windows you need to run these commands from a Visual Studio 2022 x64 command prompt. Additionally replace `~lib/my-llvm-18` with a different path.

```bash
# checkout llvm
git clone -b release/18.x --single-branch https://github.com/llvm/llvm-project.git
cd llvm-project

# build LLVM with custom installation directory
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_PARALLEL_LINK_JOBS=1
cmake --build build
cmake --install build --prefix ~/lib/my-llvm-18

# export path
export NYXSTONE_LLVM_PREFIX=~/lib/my-llvm-18
```

Also make sure to install any system dependent libraries needed by your LLVM version for static linking. They can be viewed with the command `llvm-config --system-libs`; the list can be empty. On Ubuntu/Debian, you will need the packages `zlib1g-dev` and `zlibstd-dev`.

### CLI Tool

Nyxstone comes with a handy [CLI tool](examples/nyxstone-cli.cpp) for quick assembly and disassembly tasks. Install boost with your distribution's package manager, checkout the Nyxstone repository, and build the tool with cmake:

```bash
# install boost on Ubuntu/Debian
apt install boost

# clone directory
git clone https://github.com/emproof-com/nyxstone
cd nyxstone

# build nyxstone
mkdir build && cd build && cmake .. && make 
```

Then, `nyxstone` can be used from the command line. Here's an output of its help menu:

```
$ ./nyxstone --help
Allowed options:
  --help                    Show this message
  --arch arg (=x86_64)      LLVM triple or architecture identifier of triple.
                            For the most common architectures, we recommend:
                            x86_32: `i686-linux-gnu`
                            x86_64: `x86_64-linux-gnu`
                            armv6m: `armv6m-none-eabi`
                            armv7m: `armv7m-none-eabi`
                            armv8m: `armv8m.main-none-eabi`
                            aarch64: `aarch64-linux-gnueabihf`
                            Using shorthand identifiers like `arm` can lead to
                            Nyxstone not being able to assemble certain
                            instructions.
  --cpu arg                 LLVM cpu specifier, refer to `llc -mtriple=ARCH
                            -mcpu=help` for a comprehensive list
  --features arg            LLVM features to enable/disable, comma seperated
                            feature strings prepended by '+' or '-' to enable or
                            disable respectively. Refer to `llc -mtriple=ARCH
                            -mattr=help` for a comprehensive list
  --address arg (=0)        Address

Assembling:
  --labels arg              Labels, for example "label0=0x10,label1=0x20"
  -A [ --assemble ] arg     Assembly

Disassembling:
  -D [ --disassemble ] arg  Byte code in hex, for example: "0203"

```

Now, we can assemble an instruction for the x86_64 architecture:

```
$ ./nyxstone --arch "x86_64" -A "mov rax, rbx"
Assembled:
    0x00000000: mov rax, rbx - [ 48 89 d8 ]
```

We can also assemble a sequence of instructions. In the following, we make use of label-based addressing and assume the first instruction is mapped to address `0xdeadbeef`:

```
$ ./nyxstone --arch "x86_64" --address 0xdeadbeef -A "cmp rax, rbx; jz .exit ; inc rax ; .exit: ret"
    0xdeadbeef: cmp rax, rbx - [ 48 39 d8 ]
    0xdeadbef2: je .exit - [ 74 03 ]
    0xdeadbef4: inc rax - [ 48 ff c0 ]
    0xdeadbef7: ret - [ c3 ]
```

We can also disassemble an instruction for the ARM32 thumb instruction set:

```
$ ./nyxstone --arch "thumbv8" -D "13 37"
Disassembled:
    0x00000000: adds r7, #19 - [ 13 37 ]
```

### C++ Library

To use Nyxstone as a C++ library, your C++ code has to be linked against Nyxstone and LLVM 15. 

The following cmake example assumes Nyxstone in a subdirectory `nyxstone` in your project:

```cmake
add_subdirectory(nyxstone)

add_executable(my_executable main.cpp)
target_link_libraries(my_executable nyxstone::nyxstone)
```

The corresponding C++ usage example:


```c++
#include <cassert>
#include <iostream>

#include "nyxstone.h"

int main(int, char**) {
    // Create the nyxstone instance:
    auto nyxstone {
        NyxstoneBuilder("x86_64")
            .build()
            .value()
    };

     // Assemble to bytes
    std::vector<uint8_t> bytes = 
        nyxstone->assemble(/*assembly=*/"mov rax, rbx", /*address=*/0x1000, /* labels= */ {}).value();

    std::vector<uint8_t> expected {0x48, 0x89, 0xd8};
    assert(bytes == expected);

    return 0;
}
```

For a comprehensive C++ example, take a look at [example.cpp](examples/example.cpp).


### Rust Bindings

To use Nyxstone as a Rust library, add it to your `Cargo.toml`and use it as shown in the following example:

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

For more instructions regarding the Rust binding, take a look at the corresponding [README](bindings/rust/README.md).

### Python Bindings

To use Nyxstone from Python, install it using pip:

```bash
pip install nyxstone
```

Then, you can use it from Python:

```
$ python -q
>>> from nyxstone import Nyxstone
>>> nyxstone = Nyxstone("x86_64")
>>> nyxstone.assemble("jne .loop", 0x1100, {".loop": 0x1000})
```

Detailed instructions are available in the corresponding [README](bindings/python/README.md).



## How it works

Nyxstone leverages public C++ API functions from LLVM such as `Target::createMCAsmParser` and `Target::createMCDisassembler` to perform assembly and disassembly tasks. Nyxstone also extends two LLVM classes, `MCELFStreamer` and `MCObjectWriter`, to inject custom logic and extract additional information. Specifically, Nyxstone augments the assembly process with the following steps:

* `ELFStreamerWrapper::emitInstruction`: Captures assembly representation and initial raw bytes of instructions if detailed instruction objects are requested by the user.

* `ObjectWriterWrapper::writeObject`: Writes the final raw bytes of instructions (with relocation adjustments) to detailed instruction objects. Furthermore, it switches raw bytes output from complete ELF file to just the .text section.

* `ObjectWriterWrapper::validate_fixups`: Conducts extra checks, such as verifying the range and alignment of relocations.

* `ObjectWriterWrapper::recordRelocation`: Applies additional relocations. `MCObjectWriter` skips some relocations that are only applied during linking. Right now, this is only relevant for the `fixup_aarch64_pcrel_adrp_imm21` relocation of the Aarch64 instruction `adrp`.

While extending LLVM classes introduces some drawbacks, like a strong dependency on a specific LLVM version, we believe this approach is still preferable over alternatives that require hard to maintain patches in the LLVM source tree.

We are committed to further reduce the project's complexity and open to suggestions for improvement. Looking ahead, we may eliminate the need to extend LLVM classes by leveraging the existing LLVM infrastructure in a smarter way or incorporating additional logic in a post-processing step.



## Roadmap

Below are some ideas and improvements we believe would significantly advance Nyxstone. The items are not listed in any particular order:

* [ ] Check thread safety

* [ ] Add support for more LLVM versions (auto select depending on found LLVM library version)

* [ ] Explore option to make LLVM apply all relocations (including `adrp`) by configuring `MCObjectWriter` differently or using a different writer

* [ ] Explore option to generate detailed instructions objects by disassembling the raw bytes output of the assembly process instead of relying on the extension of LLVM classes

* [ ] Explore option to implement extra range and alignment of relocations in a post-processing step instead of relying on the extension of LLVM classes


## License

Nyxstone is available under the [MIT license](LICENSE).

## Contributing

We welcome contributions from the community! If you encounter any issues with Nyxstone, please feel free to open a GitHub issue. 

If you are interested in contributing directly to the project, you can for example:

* Address an existing issue
* Develop new features
* Improve documentation

Once you're ready, submit a pull request with your changes. We are looking forward to your contribution!

## Contributors

The current contributors are:

**Core**:
* Philipp Koppe (emproof)
* Rachid Mzannar (emproof)
* Darius Hartlief (emproof)

**Minor**:
* Marc Fyrbiak (emproof)
* Tim Blazytko (emproof)

## Acknowledgements

To ensure that we link LLVM correctly with proper versioning in Rust, we adapted the build.rs from [llvm-sys](https://gitlab.com/taricorp/llvm-sys.rs/).
