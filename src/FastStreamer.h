#pragma once

#include "nyxstone.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/Config/llvm-config.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/MCSymbol.h>
#pragma GCC diagnostic pop

#include <vector>

namespace nyxstone {
/// Records the ordered sequence of parser callbacks (`emitLabel`,
/// `emitInstruction`, and data directives like `.byte` / `.int` / `.align`),
/// deferring all encoding to a single post-parse pass run by
/// `Nyxstone::assemble_impl`. By skipping the full MC object-file emission
/// pipeline, the assembler avoids the per-call ELF section/symbol bookkeeping
/// that dominated the previous path's profile.
class FastStreamer : public llvm::MCStreamer {
public:
    enum class EventKind : uint8_t {
        Label,
        Instruction,
        Value, // .byte / .short / .int / .quad … — an integer expression of `value_size` bytes
        Bytes, // .ascii / .asciz / .string … — literal bytes
        Align, // .align / .p2align / .balign — padding to an alignment boundary
        Fill, // .space / .skip / .zero / .fill — `value_expr` copies of `fill_value`, each `fill_unit` bytes
        Leb, // .uleb128 / .sleb128 — variable-length integer encoding of `value_expr`
        Org, // .org — advance to section offset `value_expr`, padding with byte `fill_value`
        Nops, // .nops — emit `nops_num_bytes` of NOP padding (optional `nops_control` max NOP length)
    };

    struct Event {
        EventKind kind {};
        llvm::MCSymbol* symbol = nullptr; // Label
        llvm::MCInst inst {}; // Instruction
        const llvm::MCSubtargetInfo* sti = nullptr; // Instruction, Align (code alignment)
        const llvm::MCExpr* value_expr = nullptr; // Value (the integer), Fill (the repeat count), Leb (the integer)
        unsigned value_size = 0; // Value
        std::string data {}; // Bytes
        unsigned alignment = 0; // Align — byte alignment (power of two)
        int64_t align_fill = 0; // Align — fill value for value alignment
        unsigned align_fill_size = 0; // Align — fill unit size; 0 means code (NOP) alignment
        unsigned align_max_bytes = 0; // Align — skip if more than this many padding bytes are needed (0 = no limit)
        int64_t fill_value = 0; // Fill — value copied into each unit; Org — fill byte
        unsigned fill_unit = 1; // Fill — bytes per copy
        bool leb_signed = false; // Leb — signed (.sleb128) vs unsigned (.uleb128)
        int64_t nops_num_bytes = 0; // Nops — total number of padding bytes
        int64_t nops_control = 0; // Nops — max length of an individual NOP (0 = backend default)
    };

    explicit FastStreamer(llvm::MCContext& ctx)
        : llvm::MCStreamer(ctx)
    {
    }

    const std::vector<Event>& events() const { return m_events; }

    void emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti) override;
    void emitLabel(llvm::MCSymbol* symbol, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override;
    void emitBytes(llvm::StringRef data) override;
    void emitValueImpl(const llvm::MCExpr* value, unsigned size, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override;
    void emitULEB128Value(const llvm::MCExpr* value) override;
    void emitSLEB128Value(const llvm::MCExpr* value) override;
    // .space / .skip / .zero — `num_bytes` copies of a single `fill_value` byte.
    void emitFill(const llvm::MCExpr& num_bytes, uint64_t fill_value, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override;
    // .fill — `num_values` copies of `expr`, each `size` bytes wide.
    void emitFill(
        const llvm::MCExpr& num_values, int64_t size, int64_t expr, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override;
    // .org — advance the location counter to `offset` (section-relative), padding with byte `value`.
    void emitValueToOffset(const llvm::MCExpr* offset, unsigned char value, llvm::SMLoc /*loc*/) override;
    // .nops — emit `num_bytes` of NOP padding, with each NOP at most `control_length` bytes (0 = backend default).
    void emitNops(
        int64_t num_bytes, int64_t control_length, llvm::SMLoc /*loc*/, const llvm::MCSubtargetInfo& sti) override;

    // Nyxstone produces a single flat `.text` blob, so a switch to any other
    // section (`.data`, `.bss`, `.section …`) would silently misplace the
    // bytes that follow. Reject it instead of dropping them.
    // The subsection parameter changed type in LLVM 19 (const MCExpr* -> uint32_t).
#if LLVM_VERSION_MAJOR < 19
    void changeSection(llvm::MCSection* section, const llvm::MCExpr* subsection) override
    {
        reject_non_text_section(section);
        llvm::MCStreamer::changeSection(section, subsection);
    }
#else
    void changeSection(llvm::MCSection* section, uint32_t subsection) override
    {
        reject_non_text_section(section);
        llvm::MCStreamer::changeSection(section, subsection);
    }
#endif

    // The alignment parameter changed type in LLVM 16 (unsigned -> Align).
#if LLVM_VERSION_MAJOR < 16
    void emitValueToAlignment(
        unsigned alignment, int64_t value = 0, unsigned value_size = 1, unsigned max_bytes = 0) override
    {
        record_alignment(alignment, value, value_size, nullptr, max_bytes);
    }
    void emitCodeAlignment(unsigned alignment, const llvm::MCSubtargetInfo* sti, unsigned max_bytes = 0) override
    {
        record_alignment(alignment, 0, 0, sti, max_bytes);
    }
#else
    void emitValueToAlignment(
        llvm::Align alignment, int64_t value = 0, unsigned value_size = 1, unsigned max_bytes = 0) override
    {
        record_alignment(static_cast<unsigned>(alignment.value()), value, value_size, nullptr, max_bytes);
    }
    void emitCodeAlignment(llvm::Align alignment, const llvm::MCSubtargetInfo* sti, unsigned max_bytes = 0) override
    {
        record_alignment(static_cast<unsigned>(alignment.value()), 0, 0, sti, max_bytes);
    }
#endif

    // Pure-virtual stubs — assemble() never produces directives that hit these.
    // The alignment parameter changed type in LLVM 16 (unsigned -> Align).
    // cppcheck-suppress unusedFunction // virtual override called via MCStreamer
    bool emitSymbolAttribute(llvm::MCSymbol* /*sym*/, llvm::MCSymbolAttr /*attr*/) override { return true; }
#if LLVM_VERSION_MAJOR < 16
    // cppcheck-suppress unusedFunction // virtual override called via MCStreamer
    void emitCommonSymbol(llvm::MCSymbol* /*sym*/, uint64_t /*size*/, unsigned /*align*/) override { }
    // cppcheck-suppress unusedFunction // virtual override called via MCStreamer
    void emitZerofill(llvm::MCSection* /*section*/, llvm::MCSymbol* /*sym*/ = nullptr, uint64_t /*size*/ = 0,
        unsigned /*align*/ = 0, llvm::SMLoc /*loc*/ = llvm::SMLoc()) override
    {
    }
#else
    // cppcheck-suppress unusedFunction // virtual override called via MCStreamer
    void emitCommonSymbol(llvm::MCSymbol* /*sym*/, uint64_t /*size*/, llvm::Align /*align*/) override { }
    // cppcheck-suppress unusedFunction // virtual override called via MCStreamer
    void emitZerofill(llvm::MCSection* /*section*/, llvm::MCSymbol* /*sym*/ = nullptr, uint64_t /*size*/ = 0,
        llvm::Align /*align*/ = llvm::Align(1), llvm::SMLoc /*loc*/ = llvm::SMLoc()) override
    {
    }
#endif

private:
    void record_alignment(
        unsigned alignment, int64_t fill, unsigned fill_size, const llvm::MCSubtargetInfo* sti, unsigned max_bytes);

    // Reports an error through the MCContext when `section` is not the single
    // `.text` section Nyxstone emits into. The MCContext diagnostic handler set
    // up by `assemble_impl` turns this into a returned error after parsing.
    void reject_non_text_section(const llvm::MCSection* section)
    {
        if (section != getContext().getObjectFileInfo()->getTextSection()) {
            getContext().reportError(
                llvm::SMLoc(), "Cannot assemble into a section other than .text (reported by Nyxstone)");
        }
    }

    std::vector<Event> m_events;
};
} // namespace nyxstone
