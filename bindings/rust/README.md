# nyxstone-rs

[![Github Rust CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/rust.yml)

Official bindings for the Nyxstone assembler/disassembler engine. 

## Building

The project can be build via `cargo build`, as long as LLVM 15 supporting static linking is installed in the `$PATH` or the environment variable `$NYXSTONE_LLVM_PREFIX` points to the installation location of such a LLVM 15 library. For further information conduct the Nyxstone README.md. The bindings further expect that the Nyxstone library is installed in the nyxstone sub-directory. This can be accomplished by using the Makefile target `nyxstone`.

## Technical overview

The nyxstone-rs bindings are generated via the `cxx` crate. Since nyxstone is specifically a c++ library, we currently do not plan to support C bindings via bindgen. 

## Future improvements

The binding class `NyxstoneFFI` currently copies a lot of data between the rust and c++ code. It would be more efficient to not copy data which is only read inside the c++ code.

## Acknowledgements

The build script of the rust bindings borrow heavily from the [llvm-sys](https://gitlab.com/taricorp/llvm-sys.rs) build script.