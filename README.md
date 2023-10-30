# Nyxstone

[![Github Cpp CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/cpp.yml)

Nyxstone is an assembly and disassembly library based on LLVM. It doesn’t require patches to the LLVM source tree and links against standard LLVM libraries available in most Linux distributions. Implemented in C++, Nyxstone also offers Rust and Python bindings. It supports all official LLVM architectures and allows configuration of architecture-specific target settings.

![Nyxstone Python binding demo](/images/demo.svg)

## Index

1. [Features](#features)
2. [Using Nyxstone](#using-nyxstone)
    1. [Prerequisites](#prerequisites)
    2. [As a rust library](#as-a-rust-library)
    3. [As a python library](#as-a-python-library)
    4. [As a C++ library](#as-a-c++-library)
    5. [The nyxstone example cli tool](#the-nyxstone-example-cli-tool)
3. [How it works](#how-it-works)
4. [License](#license)
5. [Contributing](#contributing)
6. [Contributers](#contributers)

## Features

- Assembles and Disassembles for all Architectures supported by LLVM 15 (x86, arm, aarch64, avr, amdgpu, risc-v, etc.).
- Allows specifying inline and external labels/relocations.
- Assemble or Disassemble to instruction information which holds the instructions address, bytecode, and assembly.
- Specify the number of instructions to disassemble from given bytecode.
- Configure additional hardware features via extension mnemonics.

> [!NOTE]
> Nyxstone was mainly developed and tested for x86_64 and ARM thumb. While we are fairly certain to generate correct 
> assembly as well as errors for these architectures, other architectures only work as good as their respective LLVM 
> backends.

## Using Nyxstone

There are multiple ways to use Nyxstone. Nyxstone was initially developed as a rust library, but the underlying c++ library and python bindings implemented via pybind11 are also available in this repository.

### Prerequisites

Building Nyxstone requires a modern compiler, we currently use `clang`.

Since the LLVM backend changes frequently with major releases, Nyxstone currently only supports LLVM major version 15.

For the c++ library, as well as the bindings, Nyxstone searches for `llvm-config` in the path
specified by the environment variable `NYXSTONE_LLVM_PREFIX` (specifically in the path `$NYXSTONE_LLVM_PREFIX/bin/`)
or in your `$PATH`. It then uses `llvm-config` to check that LLVM supports static linking and is major version 15
Then, all required LLVM libraries are searched via `llvm-config` and added as linker flags.

It is thus required for LLVM 15 to be installed on your system to build Nyxstone.
For newer Ubuntu/Debian versions LLVM 15 can be installed with the packages `llvm-15` and `llvm-15-dev`.
Note that these packages still require you to specify `NYXSTONE_LLVM_PREFIX=/usr/lib/llvm-15/`.

Otherwise you can install LLVM 15 by
 - Using an external package manager, like `brew`
 - Building LLVM from source

Also make sure to install any libraries needed by your LLVM version for static linking.
These depend on your system, use `llvm-config --system-libs` to see the system linker flags needed when using LLVM.
On Ubuntu/Debian you will need the packages `zlib1g-dev` and `zlibstd-dev`.

### As a rust library

See the rust bindings [README](bindings/rust/README.md).

### As a python library

See the python bindings [README](bindings/python/README.md).

### As a c++ library

Nyxstone can be used by including its header `nyxstone.h`. Following is a small c++ example in which the creation
and usage of Nyxstone objects is presented. When building your program, make sure to link against LLVM 15.

Following is a small cmake example for linking against nyxstone. Nyxstone should be a in the subdirectory `nyxstone` in your project.

```cmake
# ...
# You can also link LLVM dynamically, as long as it is LLVM 15.

# search a custom directory for LLVM 15
find_package(LLVM 15 CONFIG PATHS your-llvm-15-path NO_DEFAULT_PATH)
# search LLVM 15 in your library directories
find_package(LLVM 15 CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs core mc AllTargetsCodeGens AllTargetsAsmParsers AllTargetsDescs AllTargetsDisassemblers AllTargetsInfos AllTargetsMCAs)

add_subdirectory(nyxstone EXCLUDE_FROM_ALL) # Add nyxstone cmake without executables
include_directories(nyxstone/include)       # Nyxstone include directory

add_executable(your-executable your-sources.cpp)

target_link_libraries(your-executable nyxstone ${llvm_libs})
```

In the following we give an example of the different usages of Nyxstone.
[//]: # (Consult the documentation (TODO: link) for more detailed information) 


```c++
#include <cassert>
#include <iostream>

#include "nyxstone.h"

int main(int, char**) {
    // Create the nyxstone instance:
    auto nyxstone {NyxstoneBuilder().with_triple("x86_64").build()};

    // Assemble to bytes
    std::vector<uint8_t> bytes {};
    nyxstone->assemble_to_bytes(/*assembly=*/"mov rax, rbx", /*address=*/0x1000, /* labels= */ {}, bytes);
    {
        std::vector<uint8_t> expected {0x48, 0x89, 0xd8};
        assert(bytes == expected);
    }

    // Assemble with additional information
    std::vector<Nyxstone::Instruction> instrs {};
    nyxstone->assemble_to_instructions(/*assembly=*/"mov rax, rbx", /*address=*/0x1000, /*labels=*/ {}, instrs);
    {
        std::vector<Nyxstone::Instruction> expected {Nyxstone::Instruction {
            /*address=*/0x1000,
            /*assembly=*/"mov rax, rbx",
            /*bytes=*/ {0x48, 0x89, 0xd8}}};
        assert(instrs == expected);
    }

    // Assemble with inline label
    nyxstone->assemble_to_instructions("je .label; nop; .label:", 0x1000, {}, instrs);
    {
        std::vector<Nyxstone::Instruction> expected {
            Nyxstone::Instruction {/*address=*/0x1000, /*assembly=*/"je .label", /*bytes=*/ {0x74, 0x01}},
            Nyxstone::Instruction {/*address=*/0x1002, /*assembly=*/"nop", /*bytes=*/ {0x90}},
        };
        assert(instrs == expected);
    }

    // Assemble with external label
    nyxstone->assemble_to_bytes("jmp .label", 0x1000, {Nyxstone::LabelDefinition {".label", 0x100}}, bytes);
    {
        std::vector<uint8_t> expected {0xe9, 0xfb, 0xf0, 0xff, 0xff};
        assert(bytes == expected);
    }

    // Disassemble some bytes
    std::string disassembly;
    nyxstone->disassemble_to_text(
        /*bytes=*/ {0x48, 0x31, 0xc0, 0x66, 0x83, 0xc4, 0x08},
        /*address=*/0x1000,
        /*count=*/0,  // Disassemble all instructions
        disassembly);
    assert(disassembly == "xor rax, rax\n"
                          "add sp, 8\n");

    // Disassemble only one instruction of the bytes
    nyxstone->disassemble_to_text(
        /*bytes=*/ {0x48, 0x31, 0xc0, 0x66, 0x83, 0xc4, 0x08},
        /*address=*/0x1000,
        /*count=*/1,  // Disassemble only one instruction
        disassembly);
    assert(disassembly == "xor rax, rax\n");

    // Disassemble with additional information
    nyxstone->disassemble_to_instructions(
        /*bytes=*/ {0x48, 0x31, 0xc0, 0x66, 0x83, 0xc4, 0x08},
        /*address=*/0x1000,
        /*count=*/0,  // Disassemble all instructions
        instrs);
    {
        std::vector<Nyxstone::Instruction> expected {
            Nyxstone::Instruction {/*address=*/0x1000, /*assembly=*/"xor rax, rax", /*bytes=*/ {0x48, 0x31, 0xc0}},
            Nyxstone::Instruction {/*address=*/0x1003, /*assembly=*/"add sp, 8", /*bytes=*/ {0x66, 0x83, 0xc4, 0x08}},
        };
        assert(instrs == expected);
    }

    // Configure nyxstone to your liking:
    nyxstone = std::move(
        NyxstoneBuilder()
            .with_triple("thumbv8")
            .with_immediate_style(NyxstoneBuilder::IntegerBase::HexPrefix)  // Change the printing style of immediates
            .with_features("+mve.fp,+fp16")  // Enable additional cpu features, here floating point instructions
            .build());

    // This fp instruction can be assembled via the new nyxstone instance
    nyxstone->assemble_to_bytes("vadd.f16 s0, s1", 0x1000, {}, bytes);
    {
        std::vector<uint8_t> expected {0x30, 0xee, 0x20, 0x09};
        assert(bytes == expected);
    }

    // And the disassembly immediates are printed in hexadecimal style with a 0x-prefix
    nyxstone->assemble_to_instructions("mov r0, #16", 0x1000, {}, instrs);
    {
        std::vector<Nyxstone::Instruction> expected {Nyxstone::Instruction {
            /*address=*/0x1000,
            /*assembly=*/"mov.w r0, #0x10",
            /*bytes=*/ {0x4f, 0xf0, 0x10, 0x00}}};
        assert(instrs == expected);
    }

    return 0;
}
```

See also the [sample program](examples/sample.cpp) and the [cli tool](examples/nyxstone-cli.cpp).

### The nyxstone example cli tool

The Nyxstone cli tool allows for on-the-fly assembling and disassembling from your command line.

Requires boosts `program_options` to be installed on your system.

After building the tool with cmake
```
$ mkdir build && cd build && cmake .. && make
```

you can print a help message:
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

As well as assemble and disassemble for different architectures:

```
$ ./nyxstone --arch "x86_64 " -A "mov rax, rbx"
Assembled:
	0x00000000: mov rax, rbx - [ 48 89 d8 ]
$ ./nyxstone --arch "x86_64" -A "je .label; add rax, rbx; .label:" --address 0x1000
Assembled:
	0x00001000: je .label - [ 74 03 ]
	0x00001002: add rax, rbx - [ 48 01 d8 ]
$ ./nyxstone --arch "x86_64" -A "jmp .label" --labels ".label=0x1000"
Assembled:
	0x00000000: jmp .label - [ e9 fb 0f 00 00 ]
$ ./nyxstone --arch "armv8" -A "eor r0, r1, r2, lsl #4"
Assembled:
	0x00000000: eor r0, r1, r2, lsl #4 - [ 02 02 21 e0 ]
$ ./nyxstone --arch "thumbv8" -D "13 37"
Disassembled:
	0x00000000: adds r7, #19 - [ 13 37 ]
```

## How it works

Generally, Nyxstone uses the LLVM `MC` objects to assemble and disassemble given assembly. For assembling it uses the `MCAsmParser` to parse the assembly and for disassembling the `MCDisassembler` to decode the instructions. Additionally, it wraps the `MCObjectWriter` and `MCELFStreamer` objects in custom classes.

The wrapper for the `MCObjectWriter` has multiple functionalities. Most importantly it allows Nyxstone to limit the assembly output to the relevant bytes instead of returning an entire ELF object file. LLVM applies some relocations during the linking step, which requires Nyxstone to do some relocations itself using the wrapper. Finally, the wrapper is used to add additional fixup validations, which are not present in LLVM. This is necessary since LLVM wrongly assembles some instructions when supplied with a label
that is slightly out of range for the instruction.

The `MCELFStreamer` wrapper is used to record information during the assembly process such as the preliminary instruction size and bytes.

## License

Nyxstone is available under the [MIT license](LICENSE).

## Contributing

If there are any issues with Nyxstone, please report them as an issue.
If you want to contribute, pick up a github issue and make a pull request.
We are really thankful about any and all contributions to this project!

## Contributers

Philipp Koppe, Rachid Mzannar, Darius Hartlief @ [emproof.com](https://www.emproof.com)
