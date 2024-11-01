#pragma once

#include "nyxstone.h"

#include <llvm/Config/llvm-config.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCAsmLayout.h>
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCExpr.h>
#include <llvm/MC/MCFixup.h>
#include <llvm/MC/MCFixupKindInfo.h>
#include <llvm/MC/MCFragment.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCSection.h>
#include <llvm/MC/MCSymbol.h>
#include <llvm/MC/MCValue.h>
#pragma GCC diagnostic pop

namespace nyxstone {
/// This class enables us to limit the final output byte stream to the
/// relevant bytes (as opposed to the whole ELF object file) and grab final
/// instruction bytes (after relaxation and fixups) (via 'writeObject()').
///
/// This class is also used to insert custom relocations and fixup validations.
/// These are necessary when a relocation is normally performed at link time or when
/// LLVM does not verify a fixup according to the specification, leading to wrong
/// output for specific instruction/label combinations.
class ObjectWriterWrapper : public llvm::MCObjectWriter {
    // Wrapped MCObjectWriter, f. i., ELFSingleObjectWriter
    std::unique_ptr<llvm::MCObjectWriter> inner_object_writer;

    /// Output stream
    llvm::raw_pwrite_stream& stream;

    /// MCContext
    llvm::MCContext& context;

    /// Write section bytes only or complete object file
    bool write_text_section_only;

    /// The start address of the assembly
    u64 start_address;

    /// Reference to the nyxstone error string
    std::string& extended_error;

    /// Sink to record instruction details
    std::vector<Nyxstone::Instruction>* instructions;

    /// @brief Additional validation checks for fixups.
    ///
    /// For some fixup kinds LLVM is missing out-of-bounds and alignment checks and
    /// produces wrong instruction bytes instead of an error message.
    ///
    /// @param fragment Fragment holding the fixups.
    /// @param Layout The ASM Layout after fixups have been applied.
    void validate_fixups(const llvm::MCFragment& fragment, const llvm::MCAsmLayout& Layout);

public:
    /// @brief Creates an ObjectWriterWrapper.
    ///
    /// The ObjectWriterWrapper is created using the wrapped MCObjectWriter, some additional llvm classes
    /// used for querying additional information from LLVM, and a (nullable) pointer to the instruction information.
    ///
    /// @param object_writer The object writer object to wrap, must implement the function used by this class.
    /// @param stream Stream (used for the @p object_writer) to write the .text section to if requested via @p
    /// write_text_section_only.
    /// @param write_text_section_only If only the .text section should be written to @p stream.
    /// @param start_address Absolute start address of the instructions
    /// @param extended_error Accumulation for llvm errors, since Exceptions might not be supported by the linked LLVM.
    /// @param instructions Instruction information for which the bytes should be corrected.
    ObjectWriterWrapper(std::unique_ptr<llvm::MCObjectWriter>&& object_writer, llvm::raw_pwrite_stream& stream,
        llvm::MCContext& context, bool write_text_section_only, u64 start_address, std::string& extended_error,
        std::vector<Nyxstone::Instruction>* instructions)
        : inner_object_writer(std::move(object_writer))
        , stream(stream)
        , context(context)
        , write_text_section_only(write_text_section_only)
        , start_address(start_address)
        , extended_error(extended_error)
        , instructions(instructions)
    {
    }

    /// @brief Creates a UniquePtr holding the the ObjectWriterWrapper
    ///
    /// @param object_writer The object writer object to wrap, must implement the function used by this class.
    /// @param stream Stream (used for the @p object_writer) to write the .text section to if requested via @p
    /// write_text_section_only.
    /// @param write_text_section_only If only the .text section should be written to @p stream.
    /// @param start_address Absolute start address of the instructions
    /// @param extended_error Accumulation for llvm errors, since Exceptions might not be supported by the linked LLVM.
    /// @param instructions Instruction information for which the bytes should be corrected.
    static std::unique_ptr<llvm::MCObjectWriter> createObjectWriterWrapper(
        std::unique_ptr<llvm::MCObjectWriter>&& object_writer, llvm::raw_pwrite_stream& stream,
        llvm::MCContext& context, bool write_text_section_only, u64 start_address, std::string& extended_error,
        std::vector<Nyxstone::Instruction>* instructions);

    /// @brief Simple function wrapper calling the wrapped object wrapper function directly.
    void executePostLayoutBinding(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout) override;

    /// @brief Internal resolve for relocations.
    ///
    /// Implements relocation for:
    /// - `adrp`
    bool resolve_relocation(llvm::MCAssembler& assembler, const llvm::MCAsmLayout& layout,
        const llvm::MCFragment* fragment, const llvm::MCFixup& fixup, llvm::MCValue target, uint64_t& fixed_value);

    /// @brief Tries to resolve relocations (that are normally resolved at link time) instead of recording them
    ///
    /// This function serves multiple purposes:
    /// - Resolve (some) relocations
    /// - Ensure relocations which can not be resolved are an error instead of invalid machine bytes.
    /// - Ensure that any missing label is correctly reported.
    ///
    /// Normally, this function records relocations, which are resolved by the linker. Since we do not have a linking
    /// step, we must assume that any relocation which would be recorded is not yet correctly assembled. Thus, we try to
    /// resolve relocations ourself, although this must be implemented on a relocation basis, and we currently
    /// implement:
    /// - `adrp`
    ///
    /// Any missing label in the assembly must be resolved by the linker and leads to this function being called,
    /// we use this fact to emit errors for missing labels.
    void recordRelocation(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout, const llvm::MCFragment* Fragment,
        const llvm::MCFixup& Fixup, llvm::MCValue Target, uint64_t& FixedValue) override;

    /// @brief Write object to the stream and update the bytes of the instruction details.
    uint64_t writeObject(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout) override;
};
} // namespace nyxstone
