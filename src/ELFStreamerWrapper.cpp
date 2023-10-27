#include "ELFStreamerWrapper.h"

#include "nyxstone.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCELFStreamer.h>

#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCAsmLayout.h>
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCExpr.h>
#include <llvm/MC/MCFixup.h>
#include <llvm/MC/MCFixupKindInfo.h>
#include <llvm/MC/MCFragment.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCSection.h>
#include <llvm/MC/MCSymbol.h>
#include <llvm/MC/MCValue.h>
#include <llvm/Support/Casting.h>
#pragma GCC diagnostic pop

#include <numeric>
#include <sstream>

using namespace llvm;

namespace nyxstone {
void ELFStreamerWrapper::emitInstruction(const MCInst& Inst, const MCSubtargetInfo& STI)
{
    MCELFStreamer::emitInstruction(Inst, STI);

    // Only record instruction details if requested
    if (instructions == nullptr) {
        return;
    }

    // Get .text section
    MCSection* text_section = nullptr;
    for (MCSection& section : getAssembler()) {
        if (section.getName().str() == ".text") {
            text_section = &section;
            break;
        }
    }
    if (text_section == nullptr) {
        extended_error += "[emitInstruction] Object has no .text section.";
        return;
    }

    // Get length of already recorded instructions
    const size_t insn_byte_length = std::accumulate(instructions->begin(), instructions->end(), static_cast<size_t>(0),
        [](size_t acc, const Nyxstone::Instruction& insn) { return acc + insn.bytes.size(); });

    // Iterate fragments
    size_t frag_byte_length = 0;
    for (MCFragment& fragment : *text_section) {
        // Get Content
        MutableArrayRef<char> contents;
        switch (fragment.getKind()) {
        default:
            continue;
        case MCFragment::FT_Data: {
            auto& data_fragment = cast<MCDataFragment>(fragment);
            contents = data_fragment.getContents();
            break;
        }
        case MCFragment::FT_Relaxable: {
            auto& relaxable_fragment = cast<MCRelaxableFragment>(fragment);
            contents = relaxable_fragment.getContents();
            break;
        }
        }
        frag_byte_length += contents.size();

        // Check for new instruction bytes
        if (frag_byte_length > insn_byte_length) {
            auto insn_length = frag_byte_length - insn_byte_length;

            // Pedantic check
            if (insn_length > contents.size()) {
                std::stringstream error_stream;
                error_stream << "Internal error (= insn_length: " << insn_length
                             << ", fragment size: " << contents.size() << " )";
                extended_error += error_stream.str();
                return;
            }

            // Copy bytes to new nyxstone instruction
            Nyxstone::Instruction new_insn {};
            auto pos = contents.size() - insn_length;
            new_insn.bytes.reserve(insn_length);
            std::copy(contents.begin() + pos, contents.end(), std::back_inserter(new_insn.bytes));

            // Print instruction assembly to nyxstone instruction
            raw_string_ostream str_stream(new_insn.assembly);
            instruction_printer.printInst(&Inst, /* Address */ 0, /* Annot */ "", STI, str_stream);

            // left trim
            new_insn.assembly.erase(0, new_insn.assembly.find_first_not_of(" \t\n\r"));
            // convert tabulators to spaces
            std::replace(new_insn.assembly.begin(), new_insn.assembly.end(), '\t', ' ');

            instructions->push_back(new_insn);
            break;
        }
    }
}

std::unique_ptr<MCStreamer> ELFStreamerWrapper::createELFStreamerWrapper(MCContext& context,
    std::unique_ptr<MCAsmBackend>&& assembler_backend, std::unique_ptr<MCObjectWriter>&& object_writer,
    std::unique_ptr<MCCodeEmitter>&& code_emitter, bool RelaxAll, std::vector<Nyxstone::Instruction>* instructions,
    std::string& extended_error, MCInstPrinter& instruction_printer)
{
    auto streamer = std::make_unique<ELFStreamerWrapper>(context, std::move(assembler_backend),
        std::move(object_writer), std::move(code_emitter), instructions, extended_error, instruction_printer);

    if (RelaxAll) {
        streamer->getAssembler().setRelaxAll(true);
    }
    return streamer;
}
} // namespace nyxstone
