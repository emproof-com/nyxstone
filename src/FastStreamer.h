#pragma once

#include "nyxstone.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/MCSymbol.h>
#pragma GCC diagnostic pop

#include <vector>

namespace nyxstone {
/// Records the ordered sequence of `emitLabel` / `emitInstruction` callbacks
/// the assembly parser issues, deferring all encoding to a single post-parse
/// pass run by `Nyxstone::assemble_impl`. By skipping the full MC object-file
/// emission pipeline, the assembler avoids the per-call ELF section/symbol
/// bookkeeping that dominated the previous path's profile.
class FastStreamer : public llvm::MCStreamer {
public:
    enum class EventKind : uint8_t { Label, Instruction };

    struct Event {
        EventKind kind;
        llvm::MCSymbol* symbol; // valid when kind == Label
        llvm::MCInst inst; // valid when kind == Instruction
        const llvm::MCSubtargetInfo* sti; // valid when kind == Instruction
    };

    explicit FastStreamer(llvm::MCContext& ctx)
        : llvm::MCStreamer(ctx)
    {
    }

    const std::vector<Event>& events() const { return m_events; }

    void emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti) override;
    void emitLabel(llvm::MCSymbol* symbol, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override;

    // Pure-virtual stubs — assemble() never produces directives that hit these.
    bool emitSymbolAttribute(llvm::MCSymbol* /*sym*/, llvm::MCSymbolAttr /*attr*/) override { return true; }
    void emitCommonSymbol(llvm::MCSymbol* /*sym*/, uint64_t /*size*/, llvm::Align /*align*/) override { }
    void emitZerofill(llvm::MCSection* /*section*/, llvm::MCSymbol* /*sym*/ = nullptr, uint64_t /*size*/ = 0,
        llvm::Align /*align*/ = llvm::Align(1), llvm::SMLoc /*loc*/ = llvm::SMLoc()) override
    {
    }

private:
    std::vector<Event> m_events;
};
} // namespace nyxstone
