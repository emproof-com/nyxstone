#pragma once

#include <cinttypes>
#include <memory>
#include <string>
#include <vector>

#include "nyxstone.h"
// cppcheck-suppress missingInclude
#include "rust/cxx.h"

struct Instruction;
struct LabelDefinition;
enum class IntegerBase : uint8_t;

// Rust compatible wrapper for Nyxstone with CXX bridge types.
// See class and function documentation in Nyxstone.h for further info.
class NyxstoneFFI {
    // Internal Nyxstone instance
    std::unique_ptr<nyxstone::Nyxstone> nyxstone;

public:
    /**
     * @brief Constructor for NyxstoneFFI.
     * @param nyxstone Unique_ptr holding the Nyxstone instance.
     */
    explicit NyxstoneFFI(std::unique_ptr<nyxstone::Nyxstone>&& nyxstone)
        : nyxstone(std::move(nyxstone))
    {
    }
    ~NyxstoneFFI() = default;

    NyxstoneFFI(const NyxstoneFFI& other) = delete;
    NyxstoneFFI(NyxstoneFFI&& other) = delete;

    rust::Vec<uint8_t> assemble_to_bytes(
        rust::str assembly, uint64_t address, rust::Slice<const LabelDefinition> labels) const;

    rust::Vec<Instruction> assemble_to_instructions(
        rust::str assembly, uint64_t address, rust::Slice<const LabelDefinition> labels) const;

    rust::String disassemble_to_text(rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const;

    rust::Vec<Instruction> disassemble_to_instructions(
        rust::Slice<const uint8_t> bytes, uint64_t address, size_t count) const;
};

/// @brief Creates a NyxstoneFFI instance for the specified triple with the specified CPU and features.
///
/// @param triple_name The triple.
/// @param cpu The cpu to be used.
/// @param features Llvm features string.
/// @param imm_style The integer representation for immediates.
std::unique_ptr<NyxstoneFFI> create_nyxstone_ffi(
    rust::str triple_name, rust::str cpu, rust::str features, IntegerBase imm_style);
