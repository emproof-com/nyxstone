# Nyxstone Python bindings

[![PyPI](https://img.shields.io/pypi/v/nyxstone.svg)](https://pypi.org/project/nyxstone)
[![Github Python CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/python.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/python.yml)

## Installation

Building the bindings requires LLVM with a major version in **15-20** installed on your system. `setup.py` resolves LLVM by running `$NYXSTONE_LLVM_PREFIX/bin/llvm-config` if `NYXSTONE_LLVM_PREFIX` is set, otherwise `llvm-config` on `$PATH`.

Install the published wheel from PyPI:

```sh
pip install nyxstone
```

Or build and install from source (run from the `bindings/python` directory):

```sh
pip install .
```

On Arch Linux (and other distros that mark the system Python as externally managed), do this inside a virtualenv:

```sh
python -m venv env
source env/bin/activate    # or activate.fish / activate.zsh
pip install .              # or `pip install nyxstone`
```

## Example

Import the `Nyxstone` and `Instruction` classes:

```python
from nyxstone import Nyxstone, Instruction
```

Create a `Nyxstone` instance for the target architecture:

```python
nyxstone = Nyxstone("x86_64")
```

Assemble and disassemble raw bytes / text:

```python
assert nyxstone.assemble("mov rax, rbx") == [0x48, 0x89, 0xd8]
assert nyxstone.disassemble([0x48, 0x89, 0xd8]) == "mov rax, rbx\n"
```

Or work with `Instruction` objects that bundle address, bytes, and assembly text:

```python
instructions = [Instruction(0x0, "mov rax, rbx", [0x48, 0x89, 0xd8])]
assert nyxstone.assemble_to_instructions("mov rax, rbx") == instructions
assert nyxstone.disassemble_to_instructions([0x48, 0x89, 0xd8]) == instructions
```

When assembling, you can supply a base address and a label-to-address map for external labels; inline labels are resolved automatically:

```python
assert nyxstone.assemble("jmp .label", address=0x1000, labels={".label": 0x1200}) == [0xe9, 0xfb, 0x01, 0x00, 0x00]
assert nyxstone.assemble("jmp .label; nop; .label:", address=0x1000) == [0xeb, 0x01, 0x90]
```

When disassembling, you can specify the base address and a maximum instruction count (`0` means "until the bytes are exhausted"):

```python
assert nyxstone.disassemble([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 0) == "xor rax, rax\nadd rax, rbx\n"
assert nyxstone.disassemble([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 1) == "xor rax, rax\n"
```

## Building

To build without installing:

```sh
python setup.py build
```

## Packaging

To produce a sdist / wheel:

```sh
python -m build
```
