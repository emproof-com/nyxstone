# nyxstone-rs

[![crates.io](https://img.shields.io/crates/v/nyxstone.svg)](https://crates.io/crates/nyxstone)
[![Github Rust CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml)

Official Rust bindings for the [Nyxstone](https://github.com/emproof-com/nyxstone) assembler/disassembler engine.

## Building

`cargo build` works as long as a supported LLVM is reachable. The build script discovers LLVM in this order:

1. `$NYXSTONE_LLVM_PREFIX`, if set searched exclusively.
2. `llvm-config-N`, `llvm-configN`, or `llvmN-config` on `$PATH`, probed newest-first for `N` in 15-20.
3. Plain `llvm-config` on `$PATH` as a final fallback.

**Supported LLVM major versions: 15-20.** Any minor/patch within those majors works.

### LLVM linking

By default the build links LLVM statically. The following Cargo features change that:

| Feature          | Effect                                                |
|------------------|-------------------------------------------------------|
| `prefer-static`  | Prefer static, fall back to dynamic.                  |
| `prefer-dynamic` | Prefer dynamic, fall back to static.                  |
| `force-static`   | Static only; fail if static archives are missing.     |
| `force-dynamic`  | Dynamic only; fail if shared libraries are missing.   |

On Linux, `cargo build` defaults to `prefer-static`. On macOS it defaults to `prefer-dynamic` (Homebrew's LLVM ships shared libraries first).

### FFI workaround

LLVM may be linked against `libffi` without `llvm-config` reporting it. If you get unresolved-symbol errors involving FFI, set `NYXSTONE_LINK_FFI=1` so Nyxstone links `libffi` explicitly.

## Installation

Add Nyxstone as a dependency:

```toml
[dependencies]
nyxstone = "0.1"
```

You will need a C/C++ compiler available, plus an LLVM in the supported range.

## Sample

A short tour of the API:

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

The Rust bindings are generated with the [`cxx`](https://cxx.rs/) crate. Because the underlying engine is a C++ library, we don't plan to ship `bindgen`-based C bindings.

## Acknowledgements

The build script borrows heavily from [llvm-sys](https://gitlab.com/taricorp/llvm-sys.rs).
