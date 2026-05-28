#include "ELFStreamerWrapper.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCFragment.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <numeric>
#include <sstream>

namespace nyxstone {
// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void ELFStreamerWrapper::emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti)
{
    llvm::MCELFStreamer::emitInstruction(inst, sti);

    if (m_instructions == nullptr) {
        return;
    }

    // Locate the .text section we are emitting into.
    llvm::MCSection* text_section = nullptr;
    for (llvm::MCSection& section : getAssembler()) {
        if (section.getName() == ".text") {
            text_section = &section;
            break;
        }
    }
    if (text_section == nullptr) {
        m_extended_error += "[emitInstruction] Object has no .text section.";
        return;
    }

    // Tentatively capture this instruction's text and its current (pre-final-
    // layout) bytes. Byte counts are corrected after relaxation/fixups in
    // `Nyxstone::assemble_impl`; the text is final here.
    const size_t recorded_bytes = std::accumulate(m_instructions->begin(), m_instructions->end(),
        static_cast<size_t>(0), [](size_t acc, const Nyxstone::Instruction& insn) { return acc + insn.bytes.size(); });

    size_t fragment_bytes = 0;
    for (llvm::MCFragment& fragment : *text_section) {
        llvm::ArrayRef<char> contents;
        switch (fragment.getKind()) {
        default:
            continue;
        case llvm::MCFragment::FT_Data:
            contents = llvm::cast<llvm::MCDataFragment>(fragment).getContents();
            break;
        case llvm::MCFragment::FT_Relaxable:
            contents = llvm::cast<llvm::MCRelaxableFragment>(fragment).getContents();
            break;
        }
        fragment_bytes += contents.size();

        if (fragment_bytes <= recorded_bytes) {
            continue;
        }

        const size_t insn_length = fragment_bytes - recorded_bytes;
        if (insn_length > contents.size()) {
            std::ostringstream error_stream;
            error_stream << "Internal error (= insn_length: " << insn_length << ", fragment size: " << contents.size()
                         << " )";
            m_extended_error += error_stream.str();
            return;
        }

        Nyxstone::Instruction new_insn {};
        const size_t pos = contents.size() - insn_length;
        new_insn.bytes.assign(contents.begin() + pos, contents.end());

        llvm::raw_string_ostream str_stream(new_insn.assembly);
        m_instruction_printer.printInst(&inst, /*Address=*/0, /*Annot=*/"", sti, str_stream);
        new_insn.assembly.erase(0, new_insn.assembly.find_first_not_of(" \t\n\r"));
        std::replace(new_insn.assembly.begin(), new_insn.assembly.end(), '\t', ' ');

        m_instructions->push_back(std::move(new_insn));
        break;
    }
}
} // namespace nyxstone
