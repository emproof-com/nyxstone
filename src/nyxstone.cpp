#include "nyxstone.h"

#include "FastStreamer.h"
#include "Target/AArch64/MCTargetDesc/AArch64FixupKinds.h"
#include "Target/AArch64/MCTargetDesc/AArch64MCExpr.h"
#include "Target/ARM/MCTargetDesc/ARMFixupKinds.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/BinaryFormat/ELF.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/MC/MCAsmBackend.h>
#if LLVM_VERSION_MAJOR < 19
// MCAsmLayout was removed in LLVM 19; in 19+ the relaxation API takes
// MCAssembler directly.
#include <llvm/MC/MCAsmLayout.h>
#endif
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#include <llvm/MC/MCExpr.h>
#include <llvm/MC/MCFixupKindInfo.h>
#include <llvm/MC/MCFragment.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCParser/MCAsmParser.h>
#include <llvm/MC/MCParser/MCTargetAsmParser.h>
#include <llvm/MC/MCSectionELF.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSymbol.h>
#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/MCValue.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#pragma GCC diagnostic pop

#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>

namespace nyxstone {
NyxstoneBuilder& NyxstoneBuilder::with_cpu(std::string&& cpu) noexcept
{
    m_cpu = std::move(cpu);
    return *this;
}

NyxstoneBuilder& NyxstoneBuilder::with_features(std::string&& features) noexcept
{
    m_features = std::move(features);
    return *this;
}

NyxstoneBuilder& NyxstoneBuilder::with_immediate_style(NyxstoneBuilder::IntegerBase style) noexcept
{
    m_imm_style = style;
    return *this;
}

tl::expected<std::unique_ptr<Nyxstone>, std::string> NyxstoneBuilder::build()
{
    // # Note
    // We observed that the initialization of LLVM (in build()) is not thread
    // safe as deadlocks appear for repeated assemble and disassmble tests in
    // Rust. Thus, we guard the initialization with a static mutex so that each
    // function call to build() is thread-safe.
    static std::mutex build_common_mutex;
    const std::lock_guard<std::mutex> lock(build_common_mutex);

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllDisassemblers();

    // Resolve architecture from user-supplied target triple name
    auto triple = llvm::Triple(llvm::Triple::normalize(m_triple));
    if (triple.getArch() == llvm::Triple::UnknownArch) {
        return tl::unexpected("Invalid architecture / LLVM target triple");
    }

    std::string lookup_target_error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.getTriple(), lookup_target_error);
    if (target == nullptr) {
        return tl::unexpected(lookup_target_error);
    }

    // Init reusable llvm info objects
    auto register_info = std::unique_ptr<llvm::MCRegisterInfo>(target->createMCRegInfo(triple.getTriple()));
    if (!register_info) {
        return tl::unexpected("Could not create LLVM object (= MCRegisterInfo )");
    }

    llvm::MCTargetOptions target_options;
    auto assembler_info
        = std::unique_ptr<llvm::MCAsmInfo>(target->createMCAsmInfo(*register_info, triple.getTriple(), target_options));
    if (!assembler_info) {
        return tl::unexpected("Could not create LLVM object (= MCAsmInfo )");
    }

    auto instruction_info = std::unique_ptr<llvm::MCInstrInfo>(target->createMCInstrInfo());
    if (!instruction_info) {
        return tl::unexpected("Could not create LLVM object (= MCInstrInfo )");
    }

    auto subtarget_info
        = std::unique_ptr<llvm::MCSubtargetInfo>(target->createMCSubtargetInfo(triple.getTriple(), m_cpu, m_features));
    if (!subtarget_info) {
        return tl::unexpected("Could not create LLVM object (= MCSubtargetInfo )");
    }

    // Create instruction printer
    // For x86 and x86_64 switch to intel assembler dialect
    auto syntax_variant = assembler_info->getAssemblerDialect();
    if (triple.getArch() == llvm::Triple::x86 || triple.getArch() == llvm::Triple::x86_64) {
        syntax_variant = 1;
    }
    auto instruction_printer = std::unique_ptr<llvm::MCInstPrinter>(
        target->createMCInstPrinter(triple, syntax_variant, *assembler_info, *instruction_info, *register_info));
    if (!instruction_printer) {
        return tl::unexpected("Could not create LLVM object (= MCInstPrinter )");
    }

    switch (m_imm_style) {
    case IntegerBase::HexSuffix:
        instruction_printer->setPrintHexStyle(llvm::HexStyle::Style::Asm);
        [[fallthrough]];
    case IntegerBase::HexPrefix:
        instruction_printer->setPrintImmHex(true);
        break;

    case IntegerBase::Dec:
    default:
        break;
    }

    auto asm_backend = std::unique_ptr<llvm::MCAsmBackend>(
        target->createMCAsmBackend(*subtarget_info, *register_info, target_options));
    if (!asm_backend) {
        return tl::unexpected("Could not create LLVM object (= MCAsmBackend )");
    }

    auto disasm_context = std::make_unique<llvm::MCContext>(
        triple, assembler_info.get(), register_info.get(), subtarget_info.get(), nullptr, &target_options);
    auto disassembler
        = std::unique_ptr<llvm::MCDisassembler>(target->createMCDisassembler(*subtarget_info, *disasm_context));
    if (!disassembler) {
        return tl::unexpected("Could not create LLVM object (= MCDisassembler )");
    }

    if (!triple.isOSBinFormatELF()) {
        std::stringstream error_stream;
        error_stream << "ELF does not support target triple '" << triple.getTriple() << "'.";
        return tl::unexpected(error_stream.str());
    }
    return std::make_unique<Nyxstone>(std::move(triple), *target, std::move(target_options), std::move(register_info),
        std::move(assembler_info), std::move(instruction_info), std::move(subtarget_info),
        std::move(instruction_printer), std::move(asm_backend), std::move(disasm_context), std::move(disassembler));
}

tl::expected<std::vector<u8>, std::string> Nyxstone::assemble(
    const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const
{
    std::vector<u8> bytes;
    return assemble_impl(assembly, address, labels, bytes, nullptr).transform([&bytes]() { return std::move(bytes); });
}

tl::expected<std::vector<Nyxstone::Instruction>, std::string> Nyxstone::assemble_to_instructions(
    const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const
{
    std::vector<Instruction> instructions;
    std::vector<u8> output_bytes;
    return assemble_impl(assembly, address, labels, output_bytes, &instructions)
        .and_then([&instructions, &output_bytes]() -> tl::expected<std::vector<Instruction>, std::string> {
            const size_t insn_byte_length = std::accumulate(instructions.begin(), instructions.end(),
                static_cast<size_t>(0), [](size_t acc, const Instruction& insn) { return acc + insn.bytes.size(); });
            if (insn_byte_length != output_bytes.size()) {
                std::stringstream error_stream;
                error_stream << "Internal error (= insn_byte_length '" << insn_byte_length << "' != output_bytes.size "
                             << output_bytes.size() << ")";
                return tl::unexpected(error_stream.str());
            }
            return std::move(instructions);
        });
}

tl::expected<std::string, std::string> Nyxstone::disassemble(
    const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const
{
    std::string disassembly;
    return disassemble_impl(bytes, address, count, &disassembly, nullptr).transform([&disassembly]() {
        return std::move(disassembly);
    });
}

tl::expected<std::vector<Nyxstone::Instruction>, std::string> Nyxstone::disassemble_to_instructions(
    const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const
{
    std::vector<Nyxstone::Instruction> instructions;
    return disassemble_impl(bytes, address, count, nullptr, &instructions).transform([&instructions]() {
        return std::move(instructions);
    });
}

namespace {
    /// Cross-version `MCCodeEmitter::encodeInstruction` wrapper. LLVM 15-16
    /// expose only the `raw_ostream&` overload; LLVM 17+ adds the public
    /// `SmallVectorImpl<char>&` overload (and makes the stream variant
    /// `protected`). We always carry our own SmallVector, so wrap it in
    /// `raw_svector_ostream` for the older versions.
    inline void encode_instruction(llvm::MCCodeEmitter& emitter, const llvm::MCInst& inst,
        llvm::SmallVectorImpl<char>& bytes, llvm::SmallVectorImpl<llvm::MCFixup>& fixups,
        const llvm::MCSubtargetInfo& sti)
    {
#if LLVM_VERSION_MAJOR < 17
        llvm::raw_svector_ostream os(bytes);
        emitter.encodeInstruction(inst, os, fixups, sti);
#else
        emitter.encodeInstruction(inst, bytes, fixups, sti);
#endif
    }

    /// Validates an ARM Thumb fixup: range and alignment checks LLVM is missing
    /// for some kinds, producing wrong bytes instead of an error. Alignment
    /// and range checks operate on the runtime addresses (`address +
    /// target_offset`, `address + fixup_offset + 4`-aligned) so that
    /// 2-byte-aligned-but-not-4-byte-aligned start addresses validate
    /// correctly.
    void validate_arm_thumb_fixup(const llvm::MCFixup& fixup, uint64_t address, uint64_t target_offset,
        uint64_t fixup_offset, llvm::MCContext& context)
    {
        if (fixup.getValue() == nullptr || fixup.getValue()->getKind() != llvm::MCExpr::SymbolRef) {
            return;
        }

        const auto kind = fixup.getTargetKind();
        const uint64_t target_addr = address + target_offset;
        // Thumb PC-relative reference point for ADR/LDR (literal) and similar
        // fixups: `Align(instr_addr + 4, 4)`.
        const uint64_t pc_aligned = (address + fixup_offset + 4) & ~uint64_t { 3 };

        if (kind == llvm::ARM::fixup_thumb_adr_pcrel_10 || kind == llvm::ARM::fixup_arm_thumb_cp) {
            if ((target_addr & 3U) != 0) {
                context.reportError(fixup.getLoc(), "misaligned label address (reported by nyxstone)");
            }
        }

        if (kind == llvm::ARM::fixup_t2_adr_pcrel_12) {
            const auto offset = static_cast<int64_t>(target_addr) - static_cast<int64_t>(pc_aligned);
            if (offset <= -4096 || offset >= 4096) {
                context.reportError(fixup.getLoc(), "out of range pc-relative fixup value (reported by Nyxstone)");
            }
        }

        if (kind == llvm::ARM::fixup_arm_thumb_br || kind == llvm::ARM::fixup_arm_thumb_bl
            || kind == llvm::ARM::fixup_arm_thumb_bcc || kind == llvm::ARM::fixup_t2_uncondbranch
            || kind == llvm::ARM::fixup_t2_condbranch) {
            if ((target_addr & 1U) != 0) {
                context.reportError(fixup.getLoc(), "misaligned label address (reported by nyxstone)");
            }
        }

        if (kind == llvm::ARM::fixup_t2_pcrel_10) {
            const auto offset = static_cast<int64_t>(target_addr) - static_cast<int64_t>(pc_aligned);
            if (offset < -1020 || offset > 1020) {
                context.reportError(fixup.getLoc(), "out of range pc-relative fixup value (reported by Nyxstone)");
            }
            if ((target_addr & 3U) != 0) {
                context.reportError(fixup.getLoc(), "misaligned label address (reported by Nyxstone)");
            }
        }
    }

    /// Validates an AArch64 ADR fixup: range check LLVM is missing.
    void validate_aarch64_fixup(const llvm::MCFixup& fixup, uint64_t target_offset, llvm::MCContext& context)
    {
        if (fixup.getTargetKind() != llvm::AArch64::fixup_aarch64_pcrel_adr_imm21) {
            return;
        }
        if (fixup.getValue() == nullptr || fixup.getValue()->getKind() != llvm::MCExpr::Target) {
            return;
        }
        const auto* sub_expr = llvm::cast<llvm::AArch64MCExpr>(fixup.getValue())->getSubExpr();
        if (sub_expr == nullptr || sub_expr->getKind() != llvm::MCExpr::SymbolRef) {
            return;
        }

        const auto offset = static_cast<int64_t>(target_offset);
        if (offset < -0x100000 || offset >= 0x100000) {
            context.reportError(fixup.getLoc(), "fixup value out of range (reported by Nyxstone)");
        }
    }
} // namespace

tl::expected<void, std::string> Nyxstone::assemble_impl(const std::string& assembly, uint64_t address,
    const std::vector<LabelDefinition>& labels, std::vector<uint8_t>& bytes,
    std::vector<Instruction>* instructions) const
{
    bytes.clear();
    if (instructions != nullptr) {
        instructions->clear();
    }

    if (assembly.empty()) {
        return {};
    }

    llvm::SourceMgr source_manager;
    source_manager.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBuffer(assembly), llvm::SMLoc());

    std::string extended_error;

    llvm::MCContext context(
        triple, assembler_info.get(), register_info.get(), subtarget_info.get(), &source_manager, &target_options);
    context.setDiagnosticHandler(
        [&extended_error](const llvm::SMDiagnostic& SMD, bool /*IsInlineAsm*/, const llvm::SourceMgr& /*SrcMgr*/,
            std::vector<const llvm::MDNode*> const& /*LocInfos*/) {
            llvm::SmallString<128> error_msg;
            llvm::raw_svector_ostream error_stream(error_msg);
            SMD.print(nullptr, error_stream, /* ShowColors */ false);
            extended_error += error_msg.c_str();
        });

    if (!triple.isOSBinFormatELF()) {
        std::stringstream error_stream;
        error_stream << "ELF does not support target triple '" << triple.getTriple() << "'.";
        return tl::unexpected(error_stream.str());
    }
    TextOnlyObjectFileInfo object_file_info;
    object_file_info.initTextOnly(context);
    context.setObjectFileInfo(&object_file_info);
    auto* text_section = object_file_info.getTextSection();

    auto code_emitter_uptr
        = std::unique_ptr<llvm::MCCodeEmitter>(target.createMCCodeEmitter(*instruction_info, context));
    if (!code_emitter_uptr) {
        return tl::unexpected("Could not create LLVM object (= MCCodeEmitter )");
    }

    // MCAsmBackend is cached on Nyxstone (no MCContext dependency). MCAssembler
    // ctor requires unique_ptr ownership of *a* backend, so create a throwaway
    // per call for that role; our own applyFixup/relax calls use the cached
    // backend directly.
    auto throwaway_backend = std::unique_ptr<llvm::MCAsmBackend>(
        target.createMCAsmBackend(*subtarget_info, *register_info, target_options));
    if (!throwaway_backend) {
        return tl::unexpected("Could not create LLVM object (= MCAsmBackend )");
    }

    llvm::SmallVector<char, 64> dummy_writer_buffer;
    llvm::raw_svector_ostream dummy_writer_stream(dummy_writer_buffer);
    auto object_writer_uptr = throwaway_backend->createObjectWriter(dummy_writer_stream);

    auto* code_emitter = code_emitter_uptr.get();
    auto* asm_backend_p = asm_backend.get();

    // MCAssembler is only used to satisfy the `const MCAssembler&` parameter of
    // MCAsmBackend::applyFixup (some backends call Asm.getContext() for error
    // reporting). The backend/emitter/writer it owns are not invoked through it.
    llvm::MCAssembler assembler(
        context, std::move(throwaway_backend), std::move(code_emitter_uptr), std::move(object_writer_uptr));

    auto streamer = std::make_unique<FastStreamer>(context);
    streamer->setUseAssemblerInfoForParsing(true);
    streamer->switchSection(text_section);

    // Dummy data fragment to attach symbol definitions to so that MCSymbol::isDefined()
    // returns true during parsing — required by the parser but never inspected for content.
    llvm::MCDataFragment dummy_fragment_storage;
    auto* dummy_fragment = &dummy_fragment_storage;

    // Inject external label definitions before parsing so the parser knows their addresses.
    for (const auto& label : labels) {
        auto* inj_symbol = context.getOrCreateSymbol(label.name);
        inj_symbol->setOffset(label.address - address);
        inj_symbol->setFragment(dummy_fragment);
    }

    auto parser
        = std::unique_ptr<llvm::MCAsmParser>(createMCAsmParser(source_manager, context, *streamer, *assembler_info));
    if (!parser) {
        return tl::unexpected("Could not create LLVM object (= MCAsmParser )");
    }

    auto target_parser = std::unique_ptr<llvm::MCTargetAsmParser>(
        target.createMCAsmParser(*subtarget_info, *parser, *instruction_info, target_options));
    if (!target_parser) {
        return tl::unexpected("Could not create LLVM object (= MCTargetAsmParser )");
    }
    parser->setAssemblerDialect(1);
    parser->setTargetParser(*target_parser);

    const bool error = parser->Run(/* NoInitialTextSection= */ true);
    if (error || !extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly";
        if (!extended_error.empty()) {
            error_stream << ": " << extended_error;
        }
        return tl::unexpected(error_stream.str());
    }

    // Encode each instruction once up front. Labels and instructions stay in the
    // same ordered vector so subsequent relaxation passes can recompute output
    // offsets after re-encoding any instruction that grew.
    struct EmittedEvent {
        bool is_label;
        size_t output_offset;
        llvm::MCSymbol* symbol; // label only
        llvm::MCInst inst; // instruction only
        const llvm::MCSubtargetInfo* sti; // instruction only
        llvm::SmallVector<char, 8> bytes; // instruction only — current encoding
        llvm::SmallVector<llvm::MCFixup, 1> fixups; // instruction only
    };
    llvm::SmallVector<EmittedEvent, 16> emitted;

    for (const auto& ev : streamer->events()) {
        if (ev.kind == FastStreamer::EventKind::Label) {
            emitted.push_back(EmittedEvent { /*is_label=*/true, /*output_offset=*/0, ev.symbol, llvm::MCInst {},
                /*sti=*/nullptr, {}, {} });
            continue;
        }
        EmittedEvent e { /*is_label=*/false, /*output_offset=*/0, /*symbol=*/nullptr, ev.inst, ev.sti, {}, {} };
        encode_instruction(*code_emitter, e.inst, e.bytes, e.fixups, *e.sti);
        emitted.push_back(std::move(e));
    }

    auto recompute_offsets = [&]() {
        size_t offset = 0;
        for (auto& e : emitted) {
            e.output_offset = offset;
            if (e.is_label) {
                e.symbol->setOffset(offset);
                e.symbol->setFragment(dummy_fragment);
            } else {
                offset += e.bytes.size();
            }
        }
    };
    recompute_offsets();

    auto evaluate_target = [&](const llvm::MCFixup& fixup, int64_t& out_value, llvm::MCValue& out_mc_value) -> bool {
        const llvm::MCExpr* expr = fixup.getValue();
        if (expr == nullptr) {
            context.reportError(fixup.getLoc(), "Fixup has no value expression (reported by Nyxstone)");
            return false;
        }
        if (!expr->evaluateAsRelocatable(out_mc_value, nullptr, &fixup)) {
            context.reportError(fixup.getLoc(), "Could not evaluate fixup expression (reported by Nyxstone)");
            return false;
        }
        if (out_mc_value.getSymA() != nullptr && !out_mc_value.getSymA()->getSymbol().isDefined()) {
            context.reportError(fixup.getLoc(), "Label undefined (reported by Nyxstone)");
            return false;
        }
        if (out_mc_value.getSymB() != nullptr && !out_mc_value.getSymB()->getSymbol().isDefined()) {
            context.reportError(fixup.getLoc(), "Label undefined (reported by Nyxstone)");
            return false;
        }
        int64_t v = out_mc_value.getConstant();
        if (out_mc_value.getSymA() != nullptr) {
            v += static_cast<int64_t>(out_mc_value.getSymA()->getSymbol().getOffset());
        }
        if (out_mc_value.getSymB() != nullptr) {
            v -= static_cast<int64_t>(out_mc_value.getSymB()->getSymbol().getOffset());
        }
        out_value = v;
        return true;
    };

    auto compute_fixup_value
        = [&](const llvm::MCFixup& fixup, int64_t target_offset, uint64_t fixup_byte_offset) -> uint64_t {
        const auto kind = fixup.getTargetKind();
        const auto info = asm_backend_p->getFixupKindInfo(fixup.getKind());
        const bool is_pcrel = (info.Flags & llvm::MCFixupKindInfo::FKF_IsPCRel) != 0;
        const bool is_aligned_down_32 = (info.Flags & llvm::MCFixupKindInfo::FKF_IsAlignedDownTo32Bits) != 0;
        if (triple.isAArch64() && kind == llvm::AArch64::fixup_aarch64_pcrel_adrp_imm21) {
            constexpr uint64_t PAGE_SIZE { 0x1000 };
            const uint64_t local_addr = address + fixup_byte_offset;
            const uint64_t target_addr = address + static_cast<uint64_t>(target_offset);
            return (target_addr & ~(PAGE_SIZE - 1)) - (local_addr & ~(PAGE_SIZE - 1));
        }
        if (is_pcrel) {
            // Include `address` so that the align-down-to-4 operation works on
            // the runtime PC. Without this, Thumb's `Align(PC, 4)` PC-relative
            // loads/ADRs at 2-byte-aligned addresses produced wrong bytes —
            // the old code papered over this by prepending a `bkpt` to force
            // alignment-parity in the output.
            uint64_t pc = address + fixup_byte_offset;
            if (is_aligned_down_32) {
                pc &= ~uint64_t { 3 };
            }
            const uint64_t target_addr = address + static_cast<uint64_t>(target_offset);
            return target_addr - pc;
        }
        return static_cast<uint64_t>(target_offset) + address;
    };

    // Relax loop. For each fixup, compute its value and ask the backend whether
    // it would need relaxation. If so and the instruction is relaxable, swap
    // for the relaxed form and restart. Mirrors MCAssembler::layoutOnce() but
    // without fragments. The Layout/Assembler ref is required by some backends'
    // relaxation predicate but unused by the ones nyxstone supports here.
#if LLVM_VERSION_MAJOR < 19
    llvm::MCAsmLayout layout(assembler);
#endif
    constexpr size_t MAX_RELAX_ITERS { 32 };
    for (size_t iter = 0; iter < MAX_RELAX_ITERS; ++iter) {
        recompute_offsets();
        bool relaxed_one = false;
        for (auto& e : emitted) {
            if (e.is_label) {
                continue;
            }
            for (const auto& fx : e.fixups) {
                int64_t target_offset = 0;
                llvm::MCValue mc_value;
                if (!evaluate_target(fx, target_offset, mc_value)) {
                    break;
                }
                const uint64_t fixup_byte_offset = e.output_offset + fx.getOffset();
                const uint64_t value = compute_fixup_value(fx, target_offset, fixup_byte_offset);

                // Only ask the backend whether the fixup needs relaxation when the
                // instruction itself supports it; for non-relaxable instructions
                // some backends return spurious "needs relaxation" answers from
                // fixupNeedsRelaxationAdvanced.
#if LLVM_VERSION_MAJOR < 19
                const bool needs_relax = asm_backend_p->mayNeedRelaxation(e.inst, *e.sti)
                    && asm_backend_p->fixupNeedsRelaxationAdvanced(
                        fx, /*Resolved=*/true, value, nullptr, layout, /*WasForced=*/false);
#else
                const bool needs_relax = asm_backend_p->mayNeedRelaxation(e.inst, *e.sti)
                    && asm_backend_p->fixupNeedsRelaxationAdvanced(
                        assembler, fx, /*Resolved=*/true, value, nullptr, /*WasForced=*/false);
#endif
                if (needs_relax) {
                    llvm::MCInst relaxed = e.inst;
                    asm_backend_p->relaxInstruction(relaxed, *e.sti);
                    e.inst = relaxed;
                    e.bytes.clear();
                    e.fixups.clear();
                    encode_instruction(*code_emitter, e.inst, e.bytes, e.fixups, *e.sti);
                    relaxed_one = true;
                    break;
                }
            }
            if (relaxed_one || !extended_error.empty()) {
                break;
            }
        }
        if (!relaxed_one || !extended_error.empty()) {
            break;
        }
    }
    if (!extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly: " << extended_error;
        return tl::unexpected(error_stream.str());
    }

    // Build the final output and apply fixups.
    llvm::SmallVector<uint8_t, 128> output;
    for (const auto& e : emitted) {
        if (!e.is_label) {
            output.append(e.bytes.begin(), e.bytes.end());
        }
    }

    for (auto& e : emitted) {
        if (e.is_label) {
            continue;
        }
        for (const auto& fx : e.fixups) {
            int64_t target_offset = 0;
            llvm::MCValue mc_value;
            if (!evaluate_target(fx, target_offset, mc_value)) {
                break;
            }
            const uint64_t fixup_byte_offset = e.output_offset + fx.getOffset();
            const uint64_t value = compute_fixup_value(fx, target_offset, fixup_byte_offset);

            llvm::MutableArrayRef<char> data(reinterpret_cast<char*>(output.data()) + e.output_offset, e.bytes.size());
            asm_backend_p->applyFixup(assembler, fx, mc_value, data, value, /*IsResolved=*/true, e.sti);
        }
        if (!extended_error.empty()) {
            break;
        }
    }
    if (!extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly: " << extended_error;
        return tl::unexpected(error_stream.str());
    }

    // Re-run the nyxstone-specific validators on the final layout. These check
    // ranges/alignments LLVM's backend misses for certain ARM/AArch64 fixups.
    for (const auto& e : emitted) {
        if (e.is_label) {
            continue;
        }
        for (const auto& fx : e.fixups) {
            int64_t target_offset = 0;
            llvm::MCValue mc_value;
            if (!evaluate_target(fx, target_offset, mc_value)) {
                break;
            }
            const uint64_t fixup_byte_offset = e.output_offset + fx.getOffset();
            if (is_ArmT16_or_ArmT32(triple)) {
                validate_arm_thumb_fixup(fx, address, static_cast<uint64_t>(target_offset), fixup_byte_offset, context);
            }
            if (triple.isAArch64()) {
                validate_aarch64_fixup(fx, static_cast<uint64_t>(target_offset), context);
            }
        }
        if (!extended_error.empty()) {
            break;
        }
    }
    if (!extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly: " << extended_error;
        return tl::unexpected(error_stream.str());
    }

    if (instructions != nullptr) {
        size_t offset = 0;
        for (const auto& e : emitted) {
            if (e.is_label) {
                continue;
            }
            const size_t n = e.bytes.size();
            Nyxstone::Instruction insn;
            insn.bytes.assign(output.begin() + offset, output.begin() + offset + n);
            std::string text;
            llvm::raw_string_ostream text_stream(text);
            instruction_printer->printInst(&e.inst, /*Address=*/0, /*Annot=*/"", *e.sti, text_stream);
            text.erase(0, text.find_first_not_of(" \t\n\r"));
            std::replace(text.begin(), text.end(), '\t', ' ');
            insn.assembly = std::move(text);
            insn.address = offset;
            instructions->push_back(std::move(insn));
            offset += n;
        }
    }

    bytes.assign(output.begin(), output.end());

    if (instructions != nullptr) {
        uint64_t current_address = address;
        for (Nyxstone::Instruction& insn : *instructions) {
            insn.address = current_address;
            current_address += insn.bytes.size();
        }
    }

    return {};
}

tl::expected<void, std::string> Nyxstone::disassemble_impl(const std::vector<uint8_t>& bytes, uint64_t address,
    size_t count, std::string* disassembly, std::vector<Instruction>* instructions) const
{
    if (disassembly == nullptr && instructions == nullptr) {
        return {};
    }

    if (disassembly != nullptr) {
        disassembly->clear();
    }
    if (instructions != nullptr) {
        instructions->clear();
    }

    if (bytes.empty()) {
        return {};
    }

    llvm::SmallString<128> error_msg;
    disasm_context->setDiagnosticHandler(
        [&error_msg](const llvm::SMDiagnostic& SMD, bool /*IsInlineAsm*/, const llvm::SourceMgr& /*SrcMgr*/,
            std::vector<const llvm::MDNode*> const& /*LocInfos*/) {
            llvm::raw_svector_ostream error_stream(error_msg);
            SMD.print(nullptr, error_stream, /* ShowColors */ false);
        });

    const llvm::ArrayRef<u8> data(bytes.data(), bytes.size());
    uint64_t pos = 0;
    uint64_t insn_count = 0;
    // Reused across iterations: avoids a heap allocation per disassembled
    // instruction. The printer appends to the string; we clear it each round.
    llvm::SmallString<64> insn_buf;
    while (true) {
        llvm::MCInst insn;
        uint64_t insn_size = 0;
        auto res = disassembler->getInstruction(insn, insn_size, data.slice(pos), address + pos, llvm::nulls());
        if (res == llvm::MCDisassembler::Fail || res == llvm::MCDisassembler::SoftFail || !error_msg.empty()) {
            std::stringstream error_stream;
            error_stream << "Could not disassemble at position " << pos << " / address " << std::hex << address + pos;
            if (!error_msg.empty()) {
                error_stream << "(= " << error_msg.c_str() << " )";
            }
            return tl::unexpected(error_stream.str());
        }

        insn_buf.clear();
        llvm::raw_svector_ostream str_stream(insn_buf);
        instruction_printer->printInst(&insn,
            /* Address */ address + pos,
            /* Annot */ "", *subtarget_info, str_stream);

        // Trim leading whitespace and replace tabs with spaces in-place.
        size_t start = 0;
        while (start < insn_buf.size()
            && (insn_buf[start] == ' ' || insn_buf[start] == '\t' || insn_buf[start] == '\n'
                || insn_buf[start] == '\r')) {
            ++start;
        }
        for (size_t i = start; i < insn_buf.size(); ++i) {
            if (insn_buf[i] == '\t') {
                insn_buf[i] = ' ';
            }
        }
        const llvm::StringRef insn_view(insn_buf.data() + start, insn_buf.size() - start);

        if (disassembly != nullptr) {
            disassembly->append(insn_view.data(), insn_view.size());
            disassembly->push_back('\n');
        }
        if (instructions != nullptr) {
            Nyxstone::Instruction new_insn;
            new_insn.address = address + pos;
            new_insn.assembly.assign(insn_view.data(), insn_view.size());
            new_insn.bytes.assign(data.begin() + pos, data.begin() + pos + insn_size);
            instructions->push_back(std::move(new_insn));
        }

        insn_count += 1;
        if (count != 0 && insn_count >= count) {
            break;
        }

        pos += insn_size;
        if (pos >= data.size()) {
            break;
        }
    }

    return {};
}

bool Nyxstone::Instruction::operator==(const Instruction& other) const
{
    return address == other.address && assembly == other.assembly && bytes == other.bytes;
}

/// Detects all ARM Thumb architectures. LLVM doesn't seem to have a short way to check this.
bool is_ArmT16_or_ArmT32(const llvm::Triple& triple)
{
    return (triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6m
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v6t2
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v7m
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v7em
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8m_baseline
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8m_mainline
        || triple.getSubArch() == llvm::Triple::SubArchType::ARMSubArch_v8_1m_mainline);
}
} // namespace nyxstone
