## Building

The python bindings can be build using 
```
python setup.py build
```

You need to have LLVM 15 (with static library support) installed to build the nyxstone bindings. The `setup.py` searches for LLVM in the `PATH` or in the directory set in the environment variable `NYXSTONE_LLVM_PREFIX`. Specifically, it searches for the binary `$NYXSTONE_LLVM_PREFIX/bin/llvm-config` and uses it to set the required libraries and cpp flags.


## Packaging

To package the python bindings, use
```
python -m build
```
