from nyxstone import NyxstoneBuilder, Instruction

nyxstone = NyxstoneBuilder().with_triple("x86_64").build()

# basic functionality
assert(nyxstone.assemble_to_bytes("mov rax, rax") == [0x48, 0x89, 0xc0])
assert(nyxstone.disassemble_to_text([0x48, 0x89, 0xc0]) == "mov rax, rax\n")

# reordering of arguments
assert(nyxstone.assemble_to_bytes(address = 0x1000, labels = {".label": 0x1200}, assembly = "mov rax, rax") == [0x48, 0x89, 0xc0])

# inline/external labels
assert(nyxstone.assemble_to_bytes("jmp .label; nop; .label:") == [0xeb, 0x01, 0x90])
assert(nyxstone.assemble_to_bytes("jmp .label", labels = {".label": 0x1000}) == [0xe9, 0xfb, 0x0f, 0x00, 0x00])
assert(nyxstone.assemble_to_instructions("jmp .label", 0x1000, {".label": 0x1200}) == [Instruction(0x1000, "jmp .label", [0xe9, 0xfb, 0x01, 0x00, 0x00])])

# disassembling using address and count
assert(nyxstone.disassemble_to_instructions([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 0) == [Instruction(0x1000, "xor rax, rax", [0x48, 0x31, 0xc0]), Instruction(0x1003, "add rax, rbx", [0x48, 0x01, 0xd8])])
assert(nyxstone.disassemble_to_instructions([0x48, 0x31, 0xc0, 0x48, 0x01, 0xd8], 0x1000, 1) == [Instruction(0x1000, "xor rax, rax", [0x48, 0x31, 0xc0])])

# specify additional features
nyxstone = NyxstoneBuilder().with_triple("thumbv8").with_features("+mve.fp,+fp16").build()
nyxstone.assemble_to_bytes("vadd.f16 s0, s1, s2")
