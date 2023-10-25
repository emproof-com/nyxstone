#include "nyxstone.h"

#include <iomanip>
#include <iostream>

using emproof::Nyxstone;
using emproof::NyxstoneBuilder;

int main(int /* argc */, char** /* argv */) {
    try {
        auto nyxstone_x86_64 = NyxstoneBuilder().with_triple("x86_64-linux-gnu").build();
        auto nyxstone_armv8m = NyxstoneBuilder().with_triple("armv8m.main-none-eabi").build();

        const std::vector<Nyxstone::LabelDefinition> labels {{".label", 0x1010}};
        std::vector<uint8_t> bytes;
        std::vector<Nyxstone::Instruction> instructions;
        std::string disassembly;

        std::cout << "assemble_to_bytes:\n";
        std::cout << "\tmov rax, rax : [ ";
        nyxstone_x86_64->assemble_to_bytes("mov rax, rax", 0x1000, labels, bytes);
        std::cout << std::hex;
        for (const auto& byte : bytes) {
            std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
        }
        std::cout << std::dec;
        std::cout << "] - expected [ 48 89 c0 ]\n";

        std::cout << "\tbne .label : [ ";
        nyxstone_armv8m->assemble_to_bytes("bne .label", 0x1000, labels, bytes);

        std::cout << std::hex;
        for (const auto& byte : bytes) {
            std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
        }
        std::cout << std::dec;
        std::cout << "] - expected [ 06 d1 ]\n";

        std::cout << std::endl << "assemble_to_instructions:" << std::endl;
        std::cout << "\tmov rax, rax : [ ";
        nyxstone_x86_64->assemble_to_instructions("mov rax, rax", 0x1000, labels, instructions);
        for (const auto& insn : instructions) {
            std::cout << std::hex;
            for (const auto& byte : insn.bytes) {
                std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
            }
            std::cout << std::dec;
        }
        std::cout << "] - expected [ 48 89 c0 ]\n";

        std::cout << "\tbne .label : [ ";
        nyxstone_armv8m->assemble_to_instructions("bne .label", 0x1000, labels, instructions);
        for (const auto& insn : instructions) {
            std::cout << std::hex;
            for (const auto& byte : insn.bytes) {
                std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
            }
            std::cout << std::dec;
        }
        std::cout << "] - expected [ 06 d1 ]\n";

        std::cout << std::endl << "disassemble_to_text:" << std::endl;
        std::cout << "\t48 89 c0 : [ ";
        nyxstone_x86_64->disassemble_to_text({0x48, 0x89, 0xc0}, 0x1000, 0, disassembly);
        disassembly.erase(disassembly.find_last_not_of(" \t\n\r") + 1);
        std::cout << disassembly;
        std::cout << " ] - expected [ mov rax, rax ]\n";

        std::cout << "\t06 d1 : [ ";
        nyxstone_armv8m->disassemble_to_text({0x06, 0xd1}, 0x1000, 0, disassembly);
        disassembly.erase(disassembly.find_last_not_of(" \t\n\r") + 1);
        std::cout << disassembly;
        std::cout << " ] - expected [ bne #12 ]\n";

    } catch (const std::exception& e) {
        std::cout << "An error occured: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
