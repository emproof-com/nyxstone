#pragma once

#include "nyxstone.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCELFStreamer.h>
#include <llvm/MC/MCObjectWriter.h>
#pragma GCC diagnostic pop

namespace nyxstone {
/// This class derives from LLVM's MCELFStreamer, which enables us to receive
/// information during assembly such as preliminary instruction size and bytes
/// before relaxation and fixups (via method 'emitInstruction()').
class ELFStreamerWrapper : public llvm::MCELFStreamer {
    // Sink to record instruction details
    std::vector<Nyxstone::Instruction>* instructions;

    // Reference to the nyxstone error string
    std::string& extended_error;

    // Instruction printer
    llvm::MCInstPrinter& instruction_printer;

public:
    /// @brief ELFStreamerWrapper constructor
    /// @param context The MCContext used for reporting errors.
    /// @param assembler_backend Backend for the wrapped MCELFStreamer.
    /// @param object_writer Writer for the wrapped MCELFStreamer.
    /// @param code_emitter Emitter for the wrapped MCELFStreamer.
    /// @param RelaxAll Relax all instructions that can be relaxed.
    /// @param instructions Vector to store instruction information.
    /// @param extended_error Reference to nyxstone error string.
    /// @param instruction_printer Instruction printer used to generate the instruction assembly.
    ELFStreamerWrapper(llvm::MCContext& context, std::unique_ptr<llvm::MCAsmBackend>&& assembler_backend,
        std::unique_ptr<llvm::MCObjectWriter>&& object_writer, std::unique_ptr<llvm::MCCodeEmitter>&& code_emitter,
        std::vector<Nyxstone::Instruction>* instructions, std::string& extended_error,
        llvm::MCInstPrinter& instruction_printer)
        : llvm::MCELFStreamer(context, std::move(assembler_backend), std::move(object_writer), std::move(code_emitter))
        , instructions(instructions)
        , extended_error(extended_error)
        , instruction_printer(instruction_printer)
    {
    }

    /// @brief Creates a UniquePtr holding the ELFStreamerWrapper.
    /// @param context The MCContext used for reporting errors.
    /// @param assembler_backend Backend for the wrapped MCELFStreamer.
    /// @param object_writer Writer for the wrapped MCELFStreamer.
    /// @param code_emitter Emitter for the wrapped MCELFStreamer.
    /// @param RelaxAll Relax all instructions that can be relaxed.
    /// @param instructions Vector to store instruction information.
    /// @param extended_error Reference to nyxstone error string.
    /// @param instruction_printer Instruction printer used to generate the instruction assembly.
    /// @return unique_ptr holding the ELFStreamerWrapper
    static std::unique_ptr<llvm::MCStreamer> createELFStreamerWrapper(llvm::MCContext& context,
        std::unique_ptr<llvm::MCAsmBackend>&& assembler_backend, std::unique_ptr<llvm::MCObjectWriter>&& object_writer,
        std::unique_ptr<llvm::MCCodeEmitter>&& code_emitter, bool RelaxAll,
        std::vector<Nyxstone::Instruction>* instructions, std::string& extended_error,
        llvm::MCInstPrinter& instruction_printer);

    /// @brief Calls `MCELFStreamer::emitInstruction` and records instruction details.
    void emitInstruction(const llvm::MCInst& Inst, const llvm::MCSubtargetInfo& STI) override;
};
} // namespace nyxstone
