#include "FastStreamer.h"

namespace nyxstone {
// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti)
{
    m_events.push_back(Event { EventKind::Instruction, nullptr, inst, &sti });
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitLabel(llvm::MCSymbol* symbol, llvm::SMLoc /*loc*/)
{
    m_events.push_back(Event { EventKind::Label, symbol, llvm::MCInst {}, nullptr });
}
} // namespace nyxstone
