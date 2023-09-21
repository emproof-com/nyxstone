# Nyxstone

## Prerequisites:
Nyxstone uses the LLVM backend to assemble & disassemble instructions. Since the LLVM backend changes frequently with major releases, we currently only support LLVM major version 15. 

When Nyxstone is being built, it first searches the directory supplied by the environment variable `$NYXSTONE_LLVM_PREFIX/bin` and afterwards the `$PATH` for `llvm-config`.  When `llvm-config` is found, Nyxstone checks for both the major version and if LLVM can be statically linked.

There are multiple ways to install LLVM 15 if your package manager does not supply it:
 - Use an external package manager, like `brew`
 - Build llvm from source
 - Ubuntu/Debian only: Use the install script from https://apt.llvm.org

 ## Acknowledgements
 The build script of the rust bindings borrow heavily from the `llvm-sys` build script.