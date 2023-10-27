#include "nyxstone_ffi.hpp"

using namespace nyxstone;

struct LabelDefinition final {
    rust::str name {};
    uint64_t address = 0;
};

struct Instruction final {
    uint64_t address = 0;
    rust::String assembly {};
    rust::Vec<uint8_t> bytes {};
};

rust::Vec<uint8_t> NyxstoneFFI::assemble_to_bytes(
    const rust::str assembly, uint64_t address, const rust::Slice<const LabelDefinition> labels) const
{
    std::vector<Nyxstone::LabelDefinition> cpp_labels {};
    cpp_labels.reserve(labels.size());
    std::transform(std::begin(labels), std::end(labels), std::back_inserter(cpp_labels), [](const auto& label) {
        return Nyxstone::LabelDefinition { std::string(label.name), label.address };
    });
    std::vector<uint8_t> cpp_bytes {};

    nyxstone->assemble_to_bytes(std::string { assembly }, address, cpp_labels, cpp_bytes);

    rust::Vec<uint8_t> bytes;
    bytes.reserve(cpp_bytes.size());
    std::copy(cpp_bytes.begin(), cpp_bytes.end(), std::back_inserter(bytes));

    return bytes;
}

rust::Vec<Instruction> NyxstoneFFI::assemble_to_instructions(
    const rust::str assembly, uint64_t address, const rust::Slice<const LabelDefinition> labels) const
{
    std::vector<Nyxstone::LabelDefinition> cpp_labels;
    cpp_labels.reserve(labels.size());
    std::transform(std::begin(labels), std::end(labels), std::back_inserter(cpp_labels), [](const auto& label) {
        return Nyxstone::LabelDefinition { std::string(label.name), label.address };
    });
    std::vector<Nyxstone::Instruction> cpp_instructions {};

    nyxstone->assemble_to_instructions(std::string { assembly }, address, cpp_labels, cpp_instructions);

    rust::Vec<Instruction> instructions {};
    instructions.reserve(cpp_instructions.size());
    for (const auto& cpp_insn : cpp_instructions) {
        rust::Vec<uint8_t> bytes;
        bytes.reserve(cpp_insn.bytes.size());
        std::copy(cpp_insn.bytes.begin(), cpp_insn.bytes.end(), std::back_inserter(bytes));
        instructions.push_back({ cpp_insn.address, rust::String(cpp_insn.assembly), bytes });
    }

    return instructions;
}

rust::String NyxstoneFFI::disassemble_to_text(
    const rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const
{
    std::vector<uint8_t> cpp_bytes;
    cpp_bytes.reserve(bytes.size());
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(cpp_bytes));
    std::string cpp_disassembly;

    nyxstone->disassemble_to_text(cpp_bytes, address, count, cpp_disassembly);

    return { cpp_disassembly };
}

rust::Vec<Instruction> NyxstoneFFI::disassemble_to_instructions(
    const rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const
{
    std::vector<uint8_t> cpp_bytes {};
    cpp_bytes.reserve(bytes.size());
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(cpp_bytes));
    std::vector<Nyxstone::Instruction> cpp_instructions {};

    nyxstone->disassemble_to_instructions(cpp_bytes, address, count, cpp_instructions);

    rust::Vec<Instruction> instructions;
    instructions.reserve(cpp_instructions.size());
    for (const auto& cpp_insn : cpp_instructions) {
        rust::Vec<uint8_t> insn_bytes;
        insn_bytes.reserve(cpp_insn.bytes.size());
        std::copy(cpp_insn.bytes.begin(), cpp_insn.bytes.end(), std::back_inserter(insn_bytes));
        instructions.push_back({ cpp_insn.address, rust::String(cpp_insn.assembly), std::move(insn_bytes) });
    }

    return instructions;
}

std::unique_ptr<NyxstoneFFI> create_nyxstone_ffi( // cppcheck-suppress unusedFunction
    const rust::str triple_name, const rust::str cpu, const rust::str features, const IntegerBase imm_style)
{
    NyxstoneBuilder::IntegerBase style = static_cast<NyxstoneBuilder::IntegerBase>(static_cast<uint8_t>(imm_style));

    return std::make_unique<NyxstoneFFI>(NyxstoneBuilder()
                                             .with_triple(std::string { triple_name })
                                             .with_cpu(std::string { cpu })
                                             .with_features(std::string { features })
                                             .with_immediate_style(style)
                                             .build());
}
