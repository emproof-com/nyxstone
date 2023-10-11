from nyxstone import NyxstoneBuilder,Nyxstone,Instruction

nstone = NyxstoneBuilder().with_triple("x86_64").build()

assert(nstone.assemble_to_bytes("mov rax, rbx", 0x0, []) == [0x48, 0x89, 0xd8])
assert(nstone.disassemble_to_text([0x48, 0x89, 0xd8], 0x0, 0) == "mov rax, rbx\n")
assert(nstone.assemble_to_instructions("mov rax, rbx", 0x0, []) == [Instruction(0x0, "mov rax, rbx", [0x48, 0x89, 0xd8])])
assert(nstone.disassemble_to_instructions([0x48, 0x89, 0xd8], 0x0, 0) == [Instruction(0x0, "mov rax, rbx", [0x48, 0x89, 0xd8])])
