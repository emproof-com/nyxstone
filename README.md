# Nyxstone

[![Github Cpp CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml)

Nyxstone is an assembly and disassembly library based on LLVM. It doesn’t require patches to the LLVM source tree and links against standard LLVM libraries available in most Linux distributions. Implemented in C++, Nyxstone also offers Rust and Python bindings. It supports all official LLVM architectures and allows configuration of architecture-specific target settings.

![Nyxstone Python binding demo](/images/demo.svg)

## Index

1. [Core Features](#core-features)
2. [Using Nyxstone](#using-nyxstone)
    1. [Prerequisites](#prerequisites)
    2. [As a Rust library](#as-a-rust-library)
    3. [As a Python library](#as-a-python-library)
    4. [As a C++ library](#as-a-c-library)
    5. [C++ CLI Tool](#c-cli-tool)
3. [How it works](#how-it-works)
4. [Roadmap](#roadmap)
5. [License](#license)
6. [Contributing](#contributing)
7. [Contributers](#contributers)

## Core Features

- Assembles and Disassembles code for all architectures supported by LLVM 15, including x86 & x86_64, ARM & AArch64, MIPS, PowerPC, AVR, AMDGPU, NVPTX, RISC-V, and more. For a comprehensive list, refer to `clang -print-targets`.
- C++ library based on LLVM with Rust and Python bindings.
- Native platform support for Linux and macOS.
- Facilitates setting a custom start address and defining labels within assembly inputs, along with the ability to specify a label-to-address mapping through an additional argument.
- Assembles and disassembles to raw bytes or text respectively, or to detailed instruction objects that include additional information such as the instruction's address, raw bytes and its assembly representation.
- Provides an option to limit the number of instructions being disassembled from a given byte array.
- Supports the configuration of architecture-specific target features such as various Instruction Set Architecture (ISA) extensions or hardware features. For a comprehensive list of features for each architecture, refer to `llc -march=ARCH -mattr=help`.

> [!NOTE]
> Disclaimer: Nyxstone has been primarily developed and tested on x86_64, AArch64 and ARM architectures. We have a high degree of confidence in its ability to accurately generate assembly and identify errors for these platforms. For other architectures, Nyxstone's effectiveness is dependent on the reliability and performance of their respective LLVM backends.

## Using Nyxstone

This section provides instructions on how to get started with Nyxstone, covering the necessary prerequisites and step-by-step guidelines for using the library with C++, Rust, and Python, as well as how to utilize the C++ CLI tool.

### Prerequisites

Nyxstone requires clang and LLVM 15 static link libraries for building. It searches `llvm-config` in `$PATH` and `$NYXSTONE_LLVM_PREFIX/bin`. There are multiple ways to install LLVM 15.

- On Debian and Ubuntu install the packages `llvm-15` and `llvm-15-dev`.
```
$ apt install llvm-15 llvm-15-dev
$ export NYXSTONE_LLVM_PREFIX=/usr/lib/llvm-15/
```

- Using an external package manager like `brew`.
```
$ brew install llvm@15
$ export NYXSTONE_LLVM_PREFIX=/opt/brew/opt/llvm@15
```

- Building LLVM from source. Consider specifying a custom installation directory with the cmake option `DCMAKE_INSTALL_PREFIX`.
```
$ git clone https://github.com/llvm/llvm-project.git && cd llvm-project
$ git checkout origin/release/15.x
$ cmake -S llvm -B build -G Ninja -DCMAKE_INSTALL_PREFIX=~/lib/my-llvm-15 -DCMAKE_BUILD_TYPE=Debug
$ ninja -C build install
$ export NYXSTONE_LLVM_PREFIX=~/lib/my-llvm-15
```

Also make sure to install any system dependent libraries needed by your LLVM version for static linking. They can be viewed with the command `$ llvm-config --system-libs`. The list might be empty. On Ubuntu/Debian you will need the packages `zlib1g-dev` and `zlibstd-dev`.

### As a Rust library

Add Nyxstone as a dependency in your `Cargo.toml`.

```rust
use anyhow::Result;
use nyxstone::{LabelDefinition, NyxstoneBuilder};

fn main() -> Result<()> {
    let nyxstone = NyxstoneBuilder::default().with_triple("x86_64").build()?;

    let bytes = nyxstone.assemble_to_bytes(
        "mov rax, rbx; cmp rax, rdx; jne .label",
        0x1000,
        &[LabelDefinition { name: ".label", address: 0x1200 }],
    )?;

    println!("Bytes: {:x?}", bytes);

    Ok(())
}
```

For more instructions regarding the Rust binding, refer to its [README](bindings/rust/README.md).

### As a Python library

Install Nyxstone with pip. On some distributions you may have to create a virtual environment.

```
$ pip install nyxstone
$ python -q
>>> from nyxstone import NyxstoneBuilder
>>> nyxstone = NyxstoneBuilder().with_triple("x86_64").build()
>>> nyxstone.assemble_to_bytes("jne .loop", 0x1100, {".loop": 0x1000})
```

For more instructions regarding the Python binding, refer to its [README](bindings/python/README.md).

### As a C++ library

Link against Nyxstone and LLVM 15. This cmake example assumes Nyxstone in a subdirectory `nyxstone` in your project.

```cmake
find_package(LLVM 15 CONFIG PATHS $ENV{NYXSTONE_LLVM_PREFIX} NO_DEFAULT_PATH)
find_package(LLVM 15 CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs core mc AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsDisassemblers AllTargetsInfos AllTargetsMCAs)

add_subdirectory(nyxstone EXCLUDE_FROM_ALL) # Add nyxstone cmake without executables
include_directories(nyxstone/include)       # Nyxstone include directory

add_executable(my_executable main.cpp)

target_link_libraries(my_executable nyxstone ${llvm_libs})
```

C++ usage example.

```c++
#include <cassert>
#include <iostream>

#include "nyxstone.h"

using namespace nyxstone;

int main(int, char**) {
    // Create the nyxstone instance:
    auto nyxstone {
        NyxstoneBuilder()
            .with_triple("x86_64")
            .build()
            .value()
    };

    // Assemble to bytes
    std::vector<uint8_t> bytes = 
        nyxstone->assemble_to_bytes(/*assembly=*/"mov rax, rbx", /*address=*/0x1000, /* labels= */ {}).value();

    std::vector<uint8_t> expected {0x48, 0x89, 0xd8};
    assert(bytes == expected);

    return 0;
}
```

For a comprehensive C++ example, refer to [example.cpp](example/example.cpp).

### C++ CLI Tool

Nyxstone also comes with a handy CLI tool for quick assembly and disassembly tasks. Install boost with your distribution's package manager and build the tool with cmake.

```
$ apt install boost
$ mkdir build && cd build && cmake .. && make # run in nyxstone folder
```

Help message output.

```
$ ./nyxstone --help
Allowed options:
  --help                    Show this message
  --arch arg (=x86_64)      LLVM triple or architecture identifier of triple,
                            for example "x86_64", "x86_64-linux-gnu", "armv8",
                            "armv8eb", "thumbv8", "aarch64"
  --address arg (=0)        Address

Assembling:
  --labels arg              Labels, for example "label0=0x10,label1=0x20"
  -A [ --assemble ] arg     Assembly

Disassembling:
  -D [ --disassemble ] arg  Byte code in hex, for example: "0203"

```

Example usage.

```
$ ./nyxstone --arch "x86_64 " -A "mov rax, rbx"
Assembled:
	0x00000000: mov rax, rbx - [ 48 89 d8 ]

$ ./nyxstone --arch "thumbv8" -D "13 37"
Disassembled:
	0x00000000: adds r7, #19 - [ 13 37 ]
```

## How it works

Nyxstone leverages public C++ API functions from LLVM such as `Target::createMCAsmParser` and `Target::createMCDisassembler` to perform assembly and disassembly tasks. Nyxstone also extends two LLVM classes - `MCELFStreamer` and `MCObjectWriter` - to inject custom logic and extract additional information. Specifically, Nyxstone augments the assembly process with the following steps: 
- `ELFStreamerWrapper::emitInstruction`
    - Capture assembly representation and initial raw bytes of instructions if detailed instruction objects are requested by the user.
- `ObjectWriterWrapper::writeObject`
    - Writes the final raw bytes of instructions, with relocation adjustments, to detailed instruction objects.
    - Switches raw bytes output from complete ELF file to just the .text section.
- `ObjectWriterWrapper::validate_fixups`
    - Conducts extra checks, such as verifying the range and alignment of relocations.
- `ObjectWriterWrapper::recordRelocation`
    - Applies additional relocations. `MCObjectWriter` skips some relocations that are only applied during linking. Right now this is only relevant for the `fixup_aarch64_pcrel_adrp_imm21` in the Aarch64 `adrp` instruction.

While extending LLVM classes introduces some drawbacks, like a strong dependency on a specific LLVM version, we believe this approach is still an improvement over alternatives that require hard to maintain patches in the LLVM source tree. We are committed to further remove complexity from the project and welcome suggestions for improvement. Looking ahead, we may eliminate the need to extend LLVM classes by leveraging the existing LLVM infrastructure in a smarter way or incorporating additional logic in a post-processing step.

## Roadmap

- [ ] Native Windows platform support
- [ ] Check thread safety
- [ ] Add support for more LLVM versions (auto select depending on found LLVM library version)
- [ ] Add dynamic linking support, e. g., Arch Linux has LLVM libraries without static linking support
- [ ] Explore option to make LLVM apply all relocations (including adrp) by configuring `MCObjectWriter` differently or using a different writer
- [ ] Explore option to generate detailed instructions objects by disassembling the raw bytes output of the assembly process instead of relying on the extension of LLVM classes
- [ ] Explore option to implement extra range and alignment of relocations in a post-processing step instead of relying on the extension of LLVM classes

## License

Nyxstone is available under the [MIT license](LICENSE).

## Contributing

We welcome contributions from the community! If you encounter any issues with Nyxstone, please feel free to open a GitHub issue. If you're interested in contributing directly to the project, you can start by addressing an existing issue and submitting a pull request.

## Contributers

Philipp Koppe, Rachid Mzannar, Darius Hartlief @ [emproof.com](https://www.emproof.com)
