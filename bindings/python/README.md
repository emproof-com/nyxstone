# Nyxstone Python bindings

[![Github Python CI Badge](https://github.com/emproof-com/nyxstone/actions/workflows/python.yml/badge.svg)](https://github.com/emproof-com/nyxstone/actions/workflows/python.yml)

## Installation

You need to have LLVM 15 (with static library support) installed to build the nyxstone bindings. The `setup.py` searches for LLVM in the `PATH` or in the directory set in the environment variable `NYXSTONE_LLVM_PREFIX`. Specifically, it searches for the binary `$NYXSTONE_LLVM_PREFIX/bin/llvm-config` and uses it to set the required libraries and cpp flags.

```
pip install .
```

If you are using Arch Linux, you will need to use a virtual environment to install nyxstone:

```
mkdir env
python -mvenv env/
source env/bin/activate[.fish|.zsh]
pip install .
```

## Example

After you have installed nyxstone, import the `Nyxstone` and `Instruction` classes from nyxstone.

```python
from nyxstone import Nyxstone, Instruction
```

Now you can create the `Nyxstone` object.

```python
nyxstone = Nyxstone("x86_64")
```

The nyxstone object can be used to assemble and disassemble for the architecture it was initialized for.

```python
assert(nyxstone.assemble("mov rax, rbx") == [0x48, 0x89, 0xd8])
assert(nyxstone.disassemble([0x48, 0x89, 0xd8]) == "mov rax, rbx\n")
```

Nyxstone can also assemble and disassemble to instruction information holding the address, bytes, and assembly of the assembled or disassembled instructions.

```python
instructions = [Instruction(0x0, "mov rax, rbx", [0x48, 0x89, 0xd8])]
assert(nyxstone.assemble_to_instructions("mov rax, rbx") == instructions)
assert(nyxstone.disassemble_to_instructions([0x48, 0x89, 0xd8]) == instructions)
```

When assembling, you can also specify the address of the instructions, as well as external labels. If you need to assemble inline labels, Nyxstone also got you covered.

```python
assert(nyxstone.assemble("jmp .label", address = 0x1000, labels = {".label": 0x1200}) == [0xe9, 0xfb, 0x01, 0x00, 0x00])
assert(nyxstone.assemble("jmp .label; nop; .label:", address = 0x1000) == [0xeb, 0x01, 0x90])
```

When disassembling, you can also specify the address, as well as the number of instructions to disassemble. Here, `0` means all instructions.

```python
assert(nyxstone.disassemble([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 0) == "xor rax, rax\nadd rax, rbx\n")
assert(nyxstone.disassemble([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 1) == "xor rax, rax\n")
```

## Building

If you just want to build the python bindings, run:
```
python setup.py build
```

## Packaging

To package the python bindings, use
```
python -m build
```
