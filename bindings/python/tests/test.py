from nyxstone import Nyxstone, Instruction, IntegerBase

nyxstone = Nyxstone("x86_64")

# basic functionality
assert nyxstone.assemble("mov rax, rax") == [0x48, 0x89, 0xC0]
assert nyxstone.disassemble([0x48, 0x89, 0xC0]) == "mov rax, rax\n"
# reordering of arguments
assert nyxstone.assemble(
    address=0x1000, labels={".label": 0x1200}, assembly="mov rax, rax"
) == [0x48, 0x89, 0xC0]

# inline/external labels
assert nyxstone.assemble("jmp .label; nop; .label:") == [0xEB, 0x01, 0x90]
assert nyxstone.assemble("jmp .label", labels={".label": 0x1000}) == [
    0xE9,
    0xFB,
    0x0F,
    0x00,
    0x00,
]
assert nyxstone.assemble_to_instructions("jmp .label", 0x1000, {".label": 0x1200}) == [
    Instruction(0x1000, "jmp .label", [0xE9, 0xFB, 0x01, 0x00, 0x00])
]

# disassembling using address and count
assert nyxstone.disassemble_to_instructions(
    [0x48, 0x31, 0xC0, 0x48, 0x01, 0xD8], 0x1000, 0
) == [
    Instruction(0x1000, "xor rax, rax", [0x48, 0x31, 0xC0]),
    Instruction(0x1003, "add rax, rbx", [0x48, 0x01, 0xD8]),
]
assert nyxstone.disassemble_to_instructions(
    [0x48, 0x31, 0xC0, 0x48, 0x01, 0xD8], 0x1000, 1
) == [Instruction(0x1000, "xor rax, rax", [0x48, 0x31, 0xC0])]

# specify additional features
nyxstone = Nyxstone("thumbv8", features="+mve.fp,+fp16")

nyxstone.assemble("vadd.f16 s0, s1, s2")

# specify the immediate style
nyxstone = Nyxstone("thumbv8", immediate_style=IntegerBase.HexPrefix)
assert nyxstone.assemble_to_instructions("add r0, r0, #1") == [
    Instruction(0x0, "add.w r0, r0, #0x1", [0x00, 0xF1, 0x01, 0x00])
]
nyxstone = Nyxstone("thumbv8", immediate_style=IntegerBase.HexSuffix)
assert nyxstone.assemble_to_instructions("add r0, r0, #1") == [
    Instruction(0x0, "add.w r0, r0, #1h", [0x00, 0xF1, 0x01, 0x00])
]

# handling an error:
try:
    invalid = nyxstone.assemble("mov r20, r20")
    assert false, "Unreachable"
except Exception as e:
    assert (
        str(e)
        == "Error during assembly: error: operand must be a register in range [r0, r14]\nmov r20, r20\n    ^\n"
    )
