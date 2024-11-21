# nyxstone-rs

[![crates.io](https://img.shields.io/crates/v/nyxstone.svg)](https://crates.io/crates/nyxstone)
[![Github Rust CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml)

Official bindings for the Nyxstone assembler/disassembler engine. 

## Building

The project can be build via `cargo build`, as long as LLVM with a major version in the range 15 to 18 is availabe to CMake
and cmake is installed on the system. If your installation of LLVM is installed in a non-standard location, you can set
`NYXSTONE_LLVM_PREFIX` to tell Nyxstone or use the `CMAKE_PREFIX_PATH` environment variable to add the directory to the
CMake search path.  

## Installation

Add nyxstone as a dependency in your `Cargo.toml`:
```
[dependencies]
nyxstone = "0.1"
```

Building nyxstone requires a C/C++ compiler to be installed on your system. Additionally, Nyxstone requires LLVM with 
a major version in the range 15-18.

## Sample

In the following is a short sample of what using Nyxstone can look like:

```rust
extern crate anyhow;
extern crate nyxstone;

use std::collections::HashMap;

use anyhow::Result;
use nyxstone::{IntegerBase, Nyxstone, NyxstoneConfig};

fn main() -> Result<()> {
    // Creating a nyxstone instance can fail, for example if the triple is invalid.
    let nyxstone = Nyxstone::new("x86_64", NyxstoneConfig::default())?;

    // Assemble a single instruction
    let instructions = nyxstone.assemble_to_instructions("xor rax, rax", 0x100)?;

    println!("Assembled: ");
    for instr in instructions {
        println!("0x{:04x}: {:15} - {:02x?}", instr.address, instr.assembly, instr.bytes);
    }

    // Assemble with a label definition
    let instructions = nyxstone.assemble_to_instructions_with(
        "mov rax, rbx; cmp rax, rdx; jne .label",
        0x100,
        &HashMap::from([(".label", 0x1200)]),
    )?;

    println!("Assembled: ");
    for instr in instructions {
        println!("0x{:04x}: {:15} - {:02x?}", instr.address, instr.assembly, instr.bytes);
    }

    let disassembly = nyxstone.disassemble(
        &[0x31, 0xd8],
        /* address= */ 0x0,
        /* #instructions= (0 = all)*/ 0,
    )?;

    assert_eq!(disassembly, "xor eax, ebx\n".to_owned());

    let config = NyxstoneConfig {
        immediate_style: IntegerBase::HexPrefix,
        ..Default::default()
    };
    let nyxstone = Nyxstone::new("x86_64", config)?;

    assert_eq!(
        nyxstone.disassemble(&[0x83, 0xc0, 0x01], 0, 0)?,
        "add eax, 0x1\n".to_owned()
    );

    Ok(())
}
```

## Technical overview

The nyxstone-rs bindings are generated via the `cxx` crate. Since nyxstone is specifically a c++ library, we currently do not plan to support C bindings via bindgen. 

## Acknowledgements

The build script of the rust bindings borrow heavily from the [llvm-sys](https://gitlab.com/taricorp/llvm-sys.rs) build script.
