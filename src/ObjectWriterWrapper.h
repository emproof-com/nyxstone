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

// This class derives from LLVM's MCObjectWriter, which enables us to include
// user-define symbols/labels into the internal address resolution process
// (via 'executePostLayoutBinding()').
//
// This class also enables us to limit the final output byte stream to the
// relevant bytes (as opposted to the whole ELF object file) and grab final
// instruction bytes (after relaxation and fixups) (via 'writeObject()').
class ObjectWriterWrapper: public llvm::MCObjectWriter {
    // Wrapped MCObjectWriter, f. i., ELFSingleObjectWriter
    std::unique_ptr<llvm::MCObjectWriter> inner_object_writer;

    // Output stream
    llvm::raw_pwrite_stream& stream;

    // MCContext
    llvm::MCContext& context;

    // Write section bytes only or complete object file
    bool write_text_section_only;

    // Reference to the nyxstone error string
    std::string& extended_error;

    // Sink to record instruction details
    std::vector<Nyxstone::Instruction>* instructions;

    //Additional validation checks for fixups.
    void validate_fixups(const llvm::MCFragment& fragment, const llvm::MCAsmLayout& Layout);

  public:
    ObjectWriterWrapper(
        std::unique_ptr<llvm::MCObjectWriter>&& object_writer,
        llvm::raw_pwrite_stream& stream,
        llvm::MCContext& context,
        bool write_text_section_only,
        std::string& extended_error,
        std::vector<Nyxstone::Instruction>* instructions) :
        inner_object_writer(std::move(object_writer)),
        stream(stream),
        context(context),
        write_text_section_only(write_text_section_only),
        extended_error(extended_error),
        instructions(instructions) {}

    void executePostLayoutBinding(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout) override;

    void recordRelocation(
        llvm::MCAssembler& Asm,
        const llvm::MCAsmLayout& Layout,
        const llvm::MCFragment* Fragment,
        const llvm::MCFixup& Fixup,
        llvm::MCValue Target,
        uint64_t& FixedValue) override;

    uint64_t writeObject(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout) override;
};

std::unique_ptr<llvm::MCObjectWriter> createObjectWriterWrapper(
    std::unique_ptr<llvm::MCObjectWriter>&& object_writer,
    llvm::raw_pwrite_stream& stream,
    llvm::MCContext& context,
    bool write_text_section_only,
    std::string& extended_error,
    std::vector<Nyxstone::Instruction>* instructions);
