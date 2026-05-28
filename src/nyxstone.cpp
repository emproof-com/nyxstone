#include "nyxstone.h"

#include "ELFStreamerWrapper.h"
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
#include <llvm/Support/LEB128.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#pragma GCC diagnostic pop

#include <array>
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

    // ARM Thumb mixes 2- and 4-byte instructions and aligns the PC down to the
    // last 4-byte boundary (`base = Align(PC, 4)`) for PC-relative loads/ADR.
    // LLVM lays the section out from offset 0, so when the real start `address`
    // is 2- but not 4-byte aligned the layout parity is wrong: LLVM then relaxes
    // to the wrong encoding size or rejects the fixup outright — and that
    // happens during layout, before any post-pass can intervene. Prepending a
    // single 2-byte `bkpt #0x42` for `address % 4 == 2` restores the parity; it
    // is stripped from the output bytes and instruction list afterwards. `bkpt`
    // is used because it has a 2-byte encoding on ARMv6/7/8-M and is unusual.
    const char* const PREPENDED_BKPT_ASSEMBLY { "bkpt #0x42\n" };
    constexpr std::array<uint8_t, 2> PREPENDED_BKPT_BYTES { 0x42, 0xbe };

    // Strips the prepended bkpt (2 bytes + leading instruction entry) added for
    // Thumb alignment. Returns false (with an error in `context`) if it is not
    // present where expected.
    bool strip_prepended_bkpt(
        std::vector<uint8_t>& bytes, std::vector<Nyxstone::Instruction>* instructions, llvm::MCContext& context)
    {
        if (instructions != nullptr) {
            if (instructions->empty() || instructions->front().bytes.size() != 2
                || !std::equal(instructions->front().bytes.begin(), instructions->front().bytes.end(),
                    PREPENDED_BKPT_BYTES.begin())) {
                context.reportError(llvm::SMLoc(), "Did not find prepended bkpt at first instruction (Nyxstone)");
                return false;
            }
            instructions->erase(instructions->begin());
        }
        if (bytes.size() < 2 || bytes[0] != PREPENDED_BKPT_BYTES[0] || bytes[1] != PREPENDED_BKPT_BYTES[1]) {
            context.reportError(llvm::SMLoc(), "Did not find prepended bkpt at first two bytes (Nyxstone)");
            return false;
        }
        bytes.erase(bytes.begin(), bytes.begin() + 2);
        return true;
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

    // See PREPENDED_BKPT_ASSEMBLY: for ARM Thumb with a 2-but-not-4-byte-aligned
    // start address, prepend a 2-byte bkpt so LLVM's section-base-0 layout has
    // the right alignment parity. The extra bytes are compensated in label
    // offsets (below) and stripped from the output (at the end).
    const bool needs_prepend = is_ArmT16_or_ArmT32(triple) && (address % 4 == 2);
    const std::string input_assembly = needs_prepend ? (PREPENDED_BKPT_ASSEMBLY + assembly) : assembly;
    // The runtime address of a byte at section offset O is `effective_base + O`;
    // the prepended bkpt shifts every section offset up by its 2 bytes.
    const uint64_t prepend_bytes = needs_prepend ? PREPENDED_BKPT_BYTES.size() : 0;
    const uint64_t effective_base = address - prepend_bytes;

    llvm::SourceMgr source_manager;
    source_manager.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBuffer(input_assembly), llvm::SMLoc());

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

    // A `.text`-only object file info: skipping the setup/bookkeeping of the
    // other ELF sections is the single largest performance win over LLVM's full
    // MCObjectFileInfo, while still letting the real MCELFStreamer pipeline run.
    TextOnlyObjectFileInfo object_file_info;
    object_file_info.initTextOnly(context);
    context.setObjectFileInfo(&object_file_info);
    auto* text_section = object_file_info.getTextSection();

    // The streamer must own its backend/emitter/writer (LLVM takes unique_ptrs),
    // so these are created per call; the version-independent `*_info` objects and
    // the instruction printer remain cached on the Nyxstone instance.
    auto code_emitter = std::unique_ptr<llvm::MCCodeEmitter>(target.createMCCodeEmitter(*instruction_info, context));
    if (!code_emitter) {
        return tl::unexpected("Could not create LLVM object (= MCCodeEmitter )");
    }
    auto assembler_backend = std::unique_ptr<llvm::MCAsmBackend>(
        target.createMCAsmBackend(*subtarget_info, *register_info, target_options));
    if (!assembler_backend) {
        return tl::unexpected("Could not create LLVM object (= MCAsmBackend )");
    }
    // The cached, MCContext-independent backend is reused for the post-layout
    // fixup queries/re-application below; the streamer owns its own per-call
    // backend (LLVM requires unique_ptr ownership) for the layout itself.
    auto* backend = asm_backend.get();

    // The full ELF object is written into this throwaway buffer by finish(); we
    // extract `.text` ourselves via MCAssembler::writeSectionData afterwards.
    llvm::SmallVector<char, 128> object_buffer;
    llvm::raw_svector_ostream object_stream(object_buffer);
    auto object_writer = assembler_backend->createObjectWriter(object_stream);
    if (!object_writer) {
        return tl::unexpected("Could not create LLVM object (= MCObjectWriter )");
    }

    auto streamer = std::make_unique<ELFStreamerWrapper>(context, std::move(assembler_backend),
        std::move(object_writer), std::move(code_emitter), instructions, extended_error, *instruction_printer);
    streamer->setUseAssemblerInfoForParsing(true);
    // Attach the target's streamer so the `ldr rX, =const` pseudo works: the ARM
    // and AArch64 parsers route its literal pool through this object (without one
    // they dereference a null target streamer and crash). It registers itself as
    // the streamer's owned target streamer, so the return value is discarded.
    target.createNullTargetStreamer(*streamer);

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

    // Set up `.text` and grab its initial data fragment so externally supplied
    // labels can be anchored (MCSymbol::isDefined() must hold during parsing).
    // LLVM lays the section out from offset 0; the runtime `address` is applied
    // only to the label offsets here and to the address-sensitive fixups below.
    streamer->initSections(false, parser->getTargetParser().getSTI());
    auto* current_section = streamer->getCurrentSectionOnly();
    if (current_section == nullptr) {
        return tl::unexpected("Could not set up the .text section.");
    }
    llvm::MCFragment* anchor_fragment = nullptr;
    for (llvm::MCFragment& fragment : *current_section) {
        if (fragment.getKind() == llvm::MCFragment::FT_Data) {
            anchor_fragment = &fragment;
            break;
        }
    }
    if (anchor_fragment == nullptr) {
        return tl::unexpected("Could not find initial data fragment.");
    }
    for (const auto& label : labels) {
        auto* inj_symbol = context.getOrCreateSymbol(label.name);
        inj_symbol->setOffset(label.address - effective_base);
        inj_symbol->setFragment(anchor_fragment);
    }

    // Parse + finalize. LLVM lays out the section, relaxes, and resolves every
    // fixup/relocation it can — including target-specific ones such as RISC-V
    // %pcrel_hi/%pcrel_lo (the `la` pseudo). That automatic, complete relocation
    // handling for all architectures is the reason this path uses MCELFStreamer.
    const bool parse_error = parser->Run(/* NoInitialTextSection= */ true);
    if (parse_error || !extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly";
        if (!extended_error.empty()) {
            error_stream << ": " << extended_error;
        }
        return tl::unexpected(error_stream.str());
    }

    auto& assembler = streamer->getAssembler();
#if LLVM_VERSION_MAJOR < 19
    // MCAsmLayout was removed in LLVM 19; before that the offset/section-data
    // queries live on the layout rather than directly on the assembler.
    llvm::MCAsmLayout layout(assembler);
#endif
    auto symbol_offset = [&](const llvm::MCSymbol& symbol) -> uint64_t {
#if LLVM_VERSION_MAJOR < 19
        return layout.getSymbolOffset(symbol);
#else
        return assembler.getSymbolOffset(symbol);
#endif
    };
    auto fragment_offset = [&](const llvm::MCFragment& fragment) -> uint64_t {
#if LLVM_VERSION_MAJOR < 19
        return layout.getFragmentOffset(&fragment);
#else
        return assembler.getFragmentOffset(fragment);
#endif
    };

    // Resolves a fixup's value expression to a section-relative offset
    // (`A - B + constant`). Returns false on an undefined symbol reference.
    auto target_section_offset = [&](const llvm::MCFixup& fixup, int64_t& out) -> bool {
        const llvm::MCExpr* expr = fixup.getValue();
        llvm::MCValue value;
        if (expr == nullptr || !expr->evaluateAsRelocatable(value, nullptr, &fixup)) {
            return false;
        }
        int64_t result = value.getConstant();
        if (const auto* sym_a = value.getSymA()) {
            if (!sym_a->getSymbol().isDefined()) {
                return false;
            }
            result += static_cast<int64_t>(symbol_offset(sym_a->getSymbol()));
        }
        if (const auto* sym_b = value.getSymB()) {
            if (!sym_b->getSymbol().isDefined()) {
                return false;
            }
            result -= static_cast<int64_t>(symbol_offset(sym_b->getSymbol()));
        }
        out = result;
        return true;
    };

    // Re-run the Nyxstone-specific validators on the final layout, and resolve
    // the one fixup family LLVM defers to link time and that depends on the
    // runtime base: AArch64 `adrp` (page-of-target minus page-of-pc). Everything
    // else is either translation-invariant or already handled (Thumb alignment
    // via the bkpt prepend), so it is left exactly as LLVM resolved it.
    const bool is_thumb = is_ArmT16_or_ArmT32(triple);
    for (llvm::MCFragment& fragment : *text_section) {
        llvm::MutableArrayRef<char> contents;
        const llvm::SmallVectorImpl<llvm::MCFixup>* fixups = nullptr;
        if (fragment.getKind() == llvm::MCFragment::FT_Data) {
            auto& data_fragment = llvm::cast<llvm::MCDataFragment>(fragment);
            contents = data_fragment.getContents();
            fixups = &data_fragment.getFixups();
        } else if (fragment.getKind() == llvm::MCFragment::FT_Relaxable) {
            auto& relaxable_fragment = llvm::cast<llvm::MCRelaxableFragment>(fragment);
            contents = relaxable_fragment.getContents();
            fixups = &relaxable_fragment.getFixups();
        } else {
            continue;
        }

        const uint64_t frag_offset = fragment_offset(fragment);
        for (const llvm::MCFixup& fixup : *fixups) {
            int64_t target_offset = 0;
            if (!target_section_offset(fixup, target_offset)) {
                context.reportError(fixup.getLoc(), "Label undefined (reported by Nyxstone)");
                continue;
            }
            const uint64_t fixup_offset = frag_offset + fixup.getOffset();

            if (is_thumb) {
                validate_arm_thumb_fixup(
                    fixup, effective_base, static_cast<uint64_t>(target_offset), fixup_offset, context);
            }
            if (triple.isAArch64()) {
                validate_aarch64_fixup(fixup, static_cast<uint64_t>(target_offset), context);
            }

            // AArch64 `adrp` is page-relative and deferred to link time by LLVM,
            // so resolve it here against the runtime base. (Thumb's Align(PC,4)
            // sensitivity is handled by the bkpt prepend, which makes LLVM's
            // layout-time computation correct, so it needs nothing here.)
            if (triple.isAArch64() && fixup.getTargetKind() == llvm::AArch64::fixup_aarch64_pcrel_adrp_imm21) {
                constexpr uint64_t PAGE_SIZE { 0x1000 };
                const uint64_t local_addr = effective_base + fixup_offset;
                const uint64_t target_addr = effective_base + static_cast<uint64_t>(target_offset);
                const uint64_t value = (target_addr & ~(PAGE_SIZE - 1)) - (local_addr & ~(PAGE_SIZE - 1));
                llvm::MCValue mc_value;
                fixup.getValue()->evaluateAsRelocatable(mc_value, nullptr, &fixup);
                backend->applyFixup(
                    assembler, fixup, mc_value, contents, value, /*IsResolved=*/true, subtarget_info.get());
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

    // Extract the final `.text` bytes (fixups applied, including our re-applied
    // address-sensitive ones above).
    llvm::SmallVector<char, 128> text_bytes;
    llvm::raw_svector_ostream text_stream(text_bytes);
#if LLVM_VERSION_MAJOR < 19
    assembler.writeSectionData(text_stream, text_section, layout);
#else
    assembler.writeSectionData(text_stream, text_section);
#endif
    bytes.assign(text_bytes.begin(), text_bytes.end());

    // Replace the tentative per-instruction bytes captured during emission with
    // the final post-layout bytes, walking the fragments in order. Data
    // directives (`.byte`/`.word`/…) are not recorded as instructions, so this
    // only fills the recorded instruction entries.
    if (instructions != nullptr) {
        size_t curr_insn = 0;
        for (llvm::MCFragment& fragment : *text_section) {
            if (curr_insn >= instructions->size()) {
                break;
            }
            if (fragment.getKind() == llvm::MCFragment::FT_Data) {
                const llvm::ArrayRef<char> contents = llvm::cast<llvm::MCDataFragment>(fragment).getContents();
                size_t pos = 0;
                while (curr_insn < instructions->size()) {
                    auto& insn_bytes = instructions->at(curr_insn).bytes;
                    const size_t insn_len = insn_bytes.size();
                    if (pos + insn_len > contents.size()) {
                        break;
                    }
                    insn_bytes.assign(contents.begin() + pos, contents.begin() + pos + insn_len);
                    pos += insn_len;
                    curr_insn++;
                }
            } else if (fragment.getKind() == llvm::MCFragment::FT_Relaxable) {
                const llvm::ArrayRef<char> contents = llvm::cast<llvm::MCRelaxableFragment>(fragment).getContents();
                instructions->at(curr_insn).bytes.assign(contents.begin(), contents.end());
                curr_insn++;
            }
        }
    }

    // Strip the alignment bkpt we prepended for Thumb (if any) from both the
    // bytes and the instruction list, so the result reflects the real input.
    if (needs_prepend && !strip_prepended_bkpt(bytes, instructions, context)) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly: " << extended_error;
        return tl::unexpected(error_stream.str());
    }

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
