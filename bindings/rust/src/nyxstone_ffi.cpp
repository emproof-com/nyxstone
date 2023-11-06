#include "nyxstone_ffi.hpp"

#include "tl/expected.hpp"

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

#define ERROR_OR(e, default) (!(e)) ? (e).error() : (default)

struct NyxstoneResult {
    std::unique_ptr<NyxstoneFFI> ok;
    rust::String error;
};

struct ByteResult {
    rust::Vec<uint8_t> ok;
    rust::String error;
};

struct InstructionResult {
    rust::Vec<Instruction> ok;
    rust::String error;
};

struct StringResult {
    rust::String ok;
    rust::String error;
};

ByteResult NyxstoneFFI::assemble_to_bytes(
    const rust::str assembly, uint64_t address, const rust::Slice<const LabelDefinition> labels) const
{
    std::vector<Nyxstone::LabelDefinition> cpp_labels {};
    cpp_labels.reserve(labels.size());
    std::transform(std::begin(labels), std::end(labels), std::back_inserter(cpp_labels), [](const auto& label) {
        return Nyxstone::LabelDefinition { std::string(label.name), label.address };
    });

    auto result
        = nyxstone->assemble_to_bytes(std::string { assembly }, address, cpp_labels).map([](const auto& cpp_bytes) {
              rust::Vec<uint8_t> bytes {};
              bytes.reserve(cpp_bytes.size());
              std::copy(cpp_bytes.begin(), cpp_bytes.end(), std::back_inserter(bytes));
              return bytes;
          });

    return ByteResult { result.value_or(rust::Vec<uint8_t> {}), ERROR_OR(result, "") };
}

InstructionResult NyxstoneFFI::assemble_to_instructions(
    const rust::str assembly, uint64_t address, const rust::Slice<const LabelDefinition> labels) const
{
    std::vector<Nyxstone::LabelDefinition> cpp_labels;
    cpp_labels.reserve(labels.size());
    std::transform(std::begin(labels), std::end(labels), std::back_inserter(cpp_labels), [](const auto& label) {
        return Nyxstone::LabelDefinition { std::string(label.name), label.address };
    });
    std::vector<Nyxstone::Instruction> cpp_instructions {};

    auto result = nyxstone->assemble_to_instructions(std::string { assembly }, address, cpp_labels)
                      .map([](const auto& cpp_instructions) {
                          rust::Vec<Instruction> instructions {};
                          instructions.reserve(cpp_instructions.size());
                          for (const auto& cpp_insn : cpp_instructions) {
                              rust::Vec<uint8_t> insn_bytes;
                              insn_bytes.reserve(cpp_insn.bytes.size());
                              std::copy(cpp_insn.bytes.begin(), cpp_insn.bytes.end(), std::back_inserter(insn_bytes));
                              instructions.push_back(
                                  { cpp_insn.address, rust::String(cpp_insn.assembly), std::move(insn_bytes) });
                          }
                          return instructions;
                      });

    return InstructionResult { result.value_or(rust::Vec<Instruction> {}), ERROR_OR(result, "") };
}

StringResult NyxstoneFFI::disassemble_to_text(
    const rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const
{
    std::vector<uint8_t> cpp_bytes;
    cpp_bytes.reserve(bytes.size());
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(cpp_bytes));
    std::string cpp_disassembly;

    auto result = nyxstone->disassemble_to_text(cpp_bytes, address, count).map([](auto&& text) {
        return rust::String { std::move(text) };
    });

    return StringResult { result.value_or(rust::String {}), ERROR_OR(result, "") };
}

InstructionResult NyxstoneFFI::disassemble_to_instructions(
    const rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const
{
    std::vector<uint8_t> cpp_bytes {};
    cpp_bytes.reserve(bytes.size());
    std::copy(bytes.begin(), bytes.end(), std::back_inserter(cpp_bytes));
    std::vector<Nyxstone::Instruction> cpp_instructions {};

    auto result
        = nyxstone->disassemble_to_instructions(cpp_bytes, address, count).map([](const auto& cpp_instructions) {
              rust::Vec<Instruction> instructions {};
              for (const auto& cpp_insn : cpp_instructions) {
                  rust::Vec<uint8_t> insn_bytes;
                  insn_bytes.reserve(cpp_insn.bytes.size());
                  std::copy(cpp_insn.bytes.begin(), cpp_insn.bytes.end(), std::back_inserter(insn_bytes));
                  instructions.push_back({ cpp_insn.address, rust::String(cpp_insn.assembly), std::move(insn_bytes) });
              }
              return instructions;
          });

    return InstructionResult { result.value_or(rust::Vec<Instruction> {}), ERROR_OR(result, "") };
}

NyxstoneResult create_nyxstone_ffi( // cppcheck-suppress unusedFunction
    const rust::str triple_name, const rust::str cpu, const rust::str features, const IntegerBase imm_style)
{
    NyxstoneBuilder::IntegerBase style = static_cast<NyxstoneBuilder::IntegerBase>(static_cast<uint8_t>(imm_style));

    auto result = NyxstoneBuilder()
                      .with_triple(std::string { triple_name })
                      .with_cpu(std::string { cpu })
                      .with_features(std::string { features })
                      .with_immediate_style(style)
                      .build();

    return NyxstoneResult { std::make_unique<NyxstoneFFI>(
                                (result) ? std::move(result.value()) : std::move(std::unique_ptr<Nyxstone>(nullptr))),
        ERROR_OR(result, "") };
}
