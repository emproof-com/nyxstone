#pragma once

#include "nyxstone.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/Config/llvm-config.h>
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCELFStreamer.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCSection.h>
#include <llvm/MC/MCSubtargetInfo.h>
#pragma GCC diagnostic pop

#include <string>
#include <vector>

namespace nyxstone {
/// A thin `MCELFStreamer` so assembly runs through LLVM's full object-emission
/// pipeline — that is what gives correct relocation handling for every target
/// (e.g. RISC-V `%pcrel_hi`/`%pcrel_lo`, the `la` pseudo). On top of the base
/// behavior it does two Nyxstone-specific things:
///   * records the assembly text of each emitted instruction, in order, so the
///     final per-instruction bytes can be filled in after layout;
///   * rejects a switch to any section other than `.text`, so directives like
///     `.data`/`.section` produce an explicit error instead of silently
///     dropping the bytes that follow (Nyxstone only emits a flat `.text`).
class ELFStreamerWrapper : public llvm::MCELFStreamer {
public:
    ELFStreamerWrapper(llvm::MCContext& context, std::unique_ptr<llvm::MCAsmBackend>&& assembler_backend,
        std::unique_ptr<llvm::MCObjectWriter>&& object_writer, std::unique_ptr<llvm::MCCodeEmitter>&& code_emitter,
        std::vector<Nyxstone::Instruction>* instructions, std::string& extended_error,
        llvm::MCInstPrinter& instruction_printer)
        : llvm::MCELFStreamer(context, std::move(assembler_backend), std::move(object_writer), std::move(code_emitter))
        , m_instructions(instructions)
        , m_extended_error(extended_error)
        , m_instruction_printer(instruction_printer)
    {
    }

    void emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti) override;

    // The subsection parameter changed type in LLVM 19 (const MCExpr* -> uint32_t).
#if LLVM_VERSION_MAJOR < 19
    void changeSection(llvm::MCSection* section, const llvm::MCExpr* subsection) override
    {
        reject_non_text_section(section);
        llvm::MCELFStreamer::changeSection(section, subsection);
    }
#else
    void changeSection(llvm::MCSection* section, uint32_t subsection) override
    {
        reject_non_text_section(section);
        llvm::MCELFStreamer::changeSection(section, subsection);
    }
#endif

private:
    void reject_non_text_section(const llvm::MCSection* section)
    {
        if (section != getContext().getObjectFileInfo()->getTextSection()) {
            getContext().reportError(
                llvm::SMLoc(), "Cannot assemble into a section other than .text (reported by Nyxstone)");
        }
    }

    std::vector<Nyxstone::Instruction>* m_instructions; // nullptr unless instruction details are requested
    std::string& m_extended_error;
    llvm::MCInstPrinter& m_instruction_printer;
};
} // namespace nyxstone
