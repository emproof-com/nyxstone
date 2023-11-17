#pragma once

#include "tl/expected.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#pragma GCC diagnostic pop

namespace nyxstone {

using u8 = uint8_t;
using u64 = uint64_t;

/// Nyxstone class for assembling and disassembling for a given architecture.
class Nyxstone {
public:
    /// @brief Defines the location of a label by absolute address.
    struct LabelDefinition {
        /// The label name
        std::string name;
        /// The absolute address of the label
        uint64_t address;
    };

    /// @brief Complete instruction information.
    struct Instruction {
        /// The absolute address of the instruction
        uint64_t address;
        /// The assembly string of the instruction
        std::string assembly;
        /// The byte code of the instruction
        std::vector<uint8_t> bytes {};

        bool operator==(const Instruction& other) const;
    };

    /// @brief Nyxstone constructor called by NyxstoneBuilder::build.
    ///
    /// @warning This function should not be called directly, use NyxstoneBuilder instead.
    ///
    /// @param triple The llvm triple used to construct the llvm objects.
    /// @param cpu The cpu string used to construct the @p subtarget_info (as it points to the data inside).
    /// @param features The cpu features used to construct the @p subtarget_info (as it points to the data inside).
    /// @param target Target for the given triple.
    /// @param target_options Empty target options (reused from creating assembler_info).
    /// @param register_info Register info for the given triple.
    /// @param assembler_info Assembler info for given triple.
    /// @param instruction_info Instruction information for the given triple.
    /// @param subtarget_info Information about the subtarget, created with @p cpu and @p features.
    /// @param instruction_printer Instruction printer for the given architecture.
    Nyxstone(llvm::Triple&& triple, const llvm::Target& target, llvm::MCTargetOptions&& target_options,
        std::unique_ptr<llvm::MCRegisterInfo>&& register_info, std::unique_ptr<llvm::MCAsmInfo>&& assembler_info,
        std::unique_ptr<llvm::MCInstrInfo>&& instruction_info, std::unique_ptr<llvm::MCSubtargetInfo>&& subtarget_info,
        std::unique_ptr<llvm::MCInstPrinter>&& instruction_printer) noexcept
        : triple(std::move(triple))
        , target(target)
        , target_options(std::move(target_options))
        , register_info(std::move(register_info))
        , assembler_info(std::move(assembler_info))
        , instruction_info(std::move(instruction_info))
        , subtarget_info(std::move(subtarget_info))
        , instruction_printer(std::move(instruction_printer))
    {
    }

    /// @brief Translates assembly instructions at a given start address to bytes.
    ///
    /// Additional label definitions by absolute address may be supplied.
    /// Does not support assembly directives that impact the layout (f. i., .section, .org).
    ///
    /// @param assembly The assembly instruction(s) to be assembled.
    /// @param address The absolute address of the first instruction.
    /// @param labels Label definitions, should hold all external labels used in the @p assembly.
    ///
    /// @return The assembled bytes on success, an error string otherwise.
    tl::expected<std::vector<u8>, std::string> assemble(
        const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const;

    /// @brief Translates assembly instructions at given start address to instruction details containing bytes.
    ///
    /// Additional label definitions by absolute address may be supplied.
    /// Does not support assembly directives that impact the layout (f. i., .section, .org).
    ///
    /// @param assembly The assembly instruction(s) to be assembled.
    /// @param address The absolute address of the first instruction.
    /// @param labels Label definitions, should hold all external labels used in the @p assembly.
    ///
    /// @return The instruction details on success, an error string otherwise.
    tl::expected<std::vector<Instruction>, std::string> assemble_to_instructions(
        const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const;

    /// @brief Translates bytes to disassembly text at given start address.
    ///
    /// @param bytes The byte code to be disassembled.
    /// @param address The absolute address of the byte code.
    /// @param count The number of instructions which should be disassembled, 0 means all.
    ///
    /// @return The disassembly on success, an error string otherwise.
    tl::expected<std::string, std::string> disassemble(
        const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const;

    /// @brief Translates bytes to instruction details containing disassembly text at given start address.
    ///
    /// @param bytes The byte code to be disassembled.
    /// @param address The absolute address of the byte code.
    /// @param count The number of instructions which should be disassembled, 0 means all.
    ///
    /// @return The instruction details on success, an error string otherwise.
    tl::expected<std::vector<Instruction>, std::string> disassemble_to_instructions(
        const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const;

private:
    // Uses LLVM to assemble instructions.
    // Utilizes some custom overloads to import user-supplied label definitions and extract instruction details.
    tl::expected<void, std::string> assemble_impl(const std::string& assembly, uint64_t address,
        const std::vector<LabelDefinition>& labels, std::vector<uint8_t>& bytes,
        std::vector<Instruction>* instructions) const;

    // Uses LLVM to disassemble instructions.
    tl::expected<void, std::string> disassemble_impl(const std::vector<uint8_t>& bytes, uint64_t address, size_t count,
        std::string* disassembly, std::vector<Instruction>* instructions) const;

    /// The LLVM triple
    llvm::Triple triple;

    // Target is a static object, thus it is safe to take its const reference.
    const llvm::Target& target;

    llvm::MCTargetOptions target_options;

    std::unique_ptr<llvm::MCRegisterInfo> register_info;
    std::unique_ptr<llvm::MCAsmInfo> assembler_info;
    std::unique_ptr<llvm::MCInstrInfo> instruction_info;
    std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info;
    std::unique_ptr<llvm::MCInstPrinter> instruction_printer;
};

/**
 * @brief Builder for Nyxstone instances.
 **/
class NyxstoneBuilder {
public:
    /// @brief Configuration options for the immediate representation in disassembly.
    // This is a uint8_t for better interoperability with rust.
    enum class IntegerBase : uint8_t {
        /// Immediates are represented in decimal format.
        Dec = 0,
        /// Immediates are represented in hex format, prepended with 0x, for example: 0xff.
        HexPrefix = 1,
        /// Immediates are represented in hex format, suffixed with h, for example: 0ffh.
        HexSuffix = 2,
    };

    /// @brief Creates a NyxstoneBuilder instance.
    /// @param triple Llvm target triple or architecture identifier of a triple.
    explicit NyxstoneBuilder(std::string&& triple)
        : m_triple(std::move(triple)) {};
    NyxstoneBuilder(const NyxstoneBuilder&) = default;
    NyxstoneBuilder(NyxstoneBuilder&&) = default;
    NyxstoneBuilder& operator=(const NyxstoneBuilder&) = default;
    NyxstoneBuilder& operator=(NyxstoneBuilder&&) = default;
    ~NyxstoneBuilder() = default;

    /// @brief Specifies the cpu for which to assemble/disassemble in nyxstone.
    ///
    /// @return Reference to the updated NyxstoneBuilder object.
    NyxstoneBuilder& with_cpu(std::string&& cpu) noexcept;

    /// @brief Specify cpu features to en-/disable in nyxstone.
    ///
    /// @return Reference to the updated NyxstoneBuilder object.
    NyxstoneBuilder& with_features(std::string&& features) noexcept;

    /// @brief Specify the style in which immediates should be represented.
    ///
    /// @return Reference to the updated NyxstoneBuilder object.
    NyxstoneBuilder& with_immediate_style(IntegerBase style) noexcept;

    /// @brief Builds a nyxstone instance from the builder.
    ///
    /// @return A unique_ptr holding the created nyxstone instance on success, an error string otherwise.
    tl::expected<std::unique_ptr<Nyxstone>, std::string> build();

private:
    ///@brief The llvm target triple.
    std::string m_triple;
    /// @brief Specific CPU for LLVM, default is empty.
    std::string m_cpu;
    /// @brief Specific CPU Features for LLVM, default is empty.
    std::string m_features;
    /// @brief In which style immediates should be represented in disassembly.
    IntegerBase m_imm_style = IntegerBase::Dec;
};

/// Detects all ARM Thumb architectures. LLVM doesn't seem to have a short way to check this.
bool is_ArmT16_or_ArmT32(const llvm::Triple& triple);
} // namespace nyxstone
