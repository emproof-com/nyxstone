#include <cassert>
#include <iostream>

#include "nyxstone.h"

using namespace nyxstone;

int main(int, char**)
{
    // Create the nyxstone instance:
    auto nyxstone { NyxstoneBuilder().with_triple("x86_64").build().value() };

    // Assemble to bytes
    std::vector<uint8_t> bytes {
        nyxstone->assemble_to_bytes(/*assembly=*/"mov rax, rbx", /*address=*/0x1000, /* labels= */ {}).value()
    };
    {
        std::vector<uint8_t> expected { 0x48, 0x89, 0xd8 };
        assert(bytes == expected);
    }

    // Assemble with additional information
    std::vector<Nyxstone::Instruction> instrs { nyxstone
                                                    ->assemble_to_instructions(
                                                        /*assembly=*/"mov rax, rbx", /*address=*/0x1000,
                                                        /*labels=*/ {})
                                                    .value() };
    {

        std::vector<Nyxstone::Instruction> expected { Nyxstone::Instruction { /*address=*/0x1000,
            /*assembly=*/"mov rax, rbx",
            /*bytes=*/ { 0x48, 0x89, 0xd8 } } };
        assert(instrs == expected);
    }

    // Assemble with inline label
    instrs = nyxstone->assemble_to_instructions("je .label; nop; .label:", 0x1000, {}).value();
    {
        std::vector<Nyxstone::Instruction> expected {
            Nyxstone::Instruction { /*address=*/0x1000, /*assembly=*/"je .label", /*bytes=*/ { 0x74, 0x01 } },
            Nyxstone::Instruction { /*address=*/0x1002, /*assembly=*/"nop", /*bytes=*/ { 0x90 } },
        };
        assert(instrs == expected);
    }

    // Assemble with external label
    bytes
        = nyxstone->assemble_to_bytes("jmp .label", 0x1000, { Nyxstone::LabelDefinition { ".label", 0x100 } }).value();
    {
        std::vector<uint8_t> expected { 0xe9, 0xfb, 0xf0, 0xff, 0xff };
        assert(bytes == expected);
    }

    // Disassemble some bytes
    std::vector<uint8_t> two_instruction_bytes
        = { 0x48, 0x31, 0xc0, 0x66, 0x83, 0xc4, 0x08 }; // xor rax, rax; add sp, 8
    std::string disassembly = nyxstone
                                  ->disassemble_to_text(
                                      /*bytes=*/two_instruction_bytes,
                                      /*address=*/0x1000,
                                      /*count=*/0 // Disassemble all instructions
                                      )
                                  .value();
    assert(disassembly
        == "xor rax, rax\n"
           "add sp, 8\n");

    // Disassemble only one instruction of the bytes
    disassembly = nyxstone
                      ->disassemble_to_text(
                          /*bytes=*/two_instruction_bytes,
                          /*address=*/0x1000,
                          /*count=*/1 // Disassemble only one instruction
                          )
                      .value();
    assert(disassembly == "xor rax, rax\n");

    // Disassemble with additional information
    instrs = nyxstone
                 ->disassemble_to_instructions(
                     /*bytes=*/ { 0x48, 0x31, 0xc0, 0x66, 0x83, 0xc4, 0x08 },
                     /*address=*/0x1000,
                     /*count=*/0 // Disassemble all instructions
                     )
                 .value();
    {
        std::vector<Nyxstone::Instruction> expected {
            Nyxstone::Instruction { /*address=*/0x1000, /*assembly=*/"xor rax, rax", /*bytes=*/ { 0x48, 0x31, 0xc0 } },
            Nyxstone::Instruction {
                /*address=*/0x1003, /*assembly=*/"add sp, 8", /*bytes=*/ { 0x66, 0x83, 0xc4, 0x08 } },
        };
        assert(instrs == expected);
    }

    // Configure nyxstone to your liking:
    nyxstone = std::move(
        NyxstoneBuilder()
            .with_triple("thumbv8")
            .with_cpu("cortex-m7")
            .with_immediate_style(NyxstoneBuilder::IntegerBase::HexPrefix) // Change the printing style of immediates
            .with_features("+mve.fp,+fp16") // Enable additional cpu features, here floating point instructions
            .build()
            .value());

    // This fp instruction can be assembled via the new nyxstone instance
    bytes = nyxstone->assemble_to_bytes("vadd.f16 s0, s1", 0x1000, {}).value();
    {
        std::vector<uint8_t> expected { 0x30, 0xee, 0x20, 0x09 };
        assert(bytes == expected);
    }

    // And the disassembly immediates are printed in hexadecimal style with a 0x-prefix
    instrs = nyxstone->assemble_to_instructions("mov r0, #16", 0x1000, {}).value();
    {
        std::vector<Nyxstone::Instruction> expected { Nyxstone::Instruction { /*address=*/0x1000,
            /*assembly=*/"mov.w r0, #0x10",
            /*bytes=*/ { 0x4f, 0xf0, 0x10, 0x00 } } };
        assert(instrs == expected);
    }

    return 0;
}
