#include "FastStreamer.h"

namespace nyxstone {
// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitInstruction(const llvm::MCInst& inst, const llvm::MCSubtargetInfo& sti)
{
    Event event;
    event.kind = EventKind::Instruction;
    event.inst = inst;
    event.sti = &sti;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitLabel(llvm::MCSymbol* symbol, llvm::SMLoc /*loc*/)
{
    Event event;
    event.kind = EventKind::Label;
    event.symbol = symbol;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitBytes(llvm::StringRef data)
{
    Event event;
    event.kind = EventKind::Bytes;
    event.data.assign(data.begin(), data.end());
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitValueImpl(const llvm::MCExpr* value, unsigned size, llvm::SMLoc /*loc*/)
{
    Event event;
    event.kind = EventKind::Value;
    event.value_expr = value;
    event.value_size = size;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitULEB128Value(const llvm::MCExpr* value)
{
    Event event;
    event.kind = EventKind::Leb;
    event.value_expr = value;
    event.leb_signed = false;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitSLEB128Value(const llvm::MCExpr* value)
{
    Event event;
    event.kind = EventKind::Leb;
    event.value_expr = value;
    event.leb_signed = true;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitFill(const llvm::MCExpr& num_bytes, uint64_t fill_value, llvm::SMLoc /*loc*/)
{
    Event event;
    event.kind = EventKind::Fill;
    event.value_expr = &num_bytes;
    event.fill_value = static_cast<int64_t>(fill_value);
    event.fill_unit = 1;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitFill(const llvm::MCExpr& num_values, int64_t size, int64_t expr, llvm::SMLoc /*loc*/)
{
    Event event;
    event.kind = EventKind::Fill;
    event.value_expr = &num_values;
    event.fill_value = expr;
    event.fill_unit = (size > 0) ? static_cast<unsigned>(size) : 1;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitValueToOffset(const llvm::MCExpr* offset, unsigned char value, llvm::SMLoc /*loc*/)
{
    Event event;
    event.kind = EventKind::Org;
    event.value_expr = offset;
    event.fill_value = value;
    m_events.push_back(std::move(event));
}

// cppcheck-suppress unusedFunction // virtual override called via MCStreamer
void FastStreamer::emitNops(
    int64_t num_bytes, int64_t control_length, llvm::SMLoc /*loc*/, const llvm::MCSubtargetInfo& sti)
{
    Event event;
    event.kind = EventKind::Nops;
    event.sti = &sti;
    event.nops_num_bytes = num_bytes;
    event.nops_control = control_length;
    m_events.push_back(std::move(event));
}

void FastStreamer::record_alignment(
    unsigned alignment, int64_t fill, unsigned fill_size, const llvm::MCSubtargetInfo* sti, unsigned max_bytes)
{
    Event event;
    event.kind = EventKind::Align;
    event.sti = sti;
    event.alignment = alignment;
    event.align_fill = fill;
    event.align_fill_size = fill_size;
    event.align_max_bytes = max_bytes;
    m_events.push_back(std::move(event));
}
} // namespace nyxstone
