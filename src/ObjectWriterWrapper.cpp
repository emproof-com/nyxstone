#include "ObjectWriterWrapper.h"

#include "nyxstone.h"
#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "Target/AArch64/MCTargetDesc/AArch64FixupKinds.h"
#include "Target/AArch64/MCTargetDesc/AArch64MCExpr.h"
#include "Target/ARM/MCTargetDesc/ARMFixupKinds.h"
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCAsmLayout.h>
#include <llvm/MC/MCAssembler.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCExpr.h>
#include <llvm/MC/MCFixup.h>
#include <llvm/MC/MCFixupKindInfo.h>
#include <llvm/MC/MCFragment.h>
#include <llvm/MC/MCObjectWriter.h>
#include <llvm/MC/MCSection.h>
#include <llvm/MC/MCSymbol.h>
#include <llvm/MC/MCValue.h>
#include <llvm/Support/Casting.h>
#pragma GCC diagnostic pop

#include <sstream>

using namespace llvm;

namespace nyxstone {
/// @brief Validates the given arm fixup with our custom validatinon routines.
///
/// @param fixup The fixup to be validated.
/// @param Layout The ASM Layout after fixups have been applied.
/// @param context The MCContext used to report errors.
void validate_arm_thumb(const MCFixup& fixup, const MCAsmLayout& Layout, MCContext& context)
{
    // For all instructions we are checking here, we need to make sure that the fixup is a SymbolRef.
    // If it is not, we do not need to check the instruction.
    const bool fixup_is_symbolref = fixup.getValue() != nullptr && fixup.getValue()->getKind() == MCExpr::SymbolRef;
    if (!fixup_is_symbolref) {
        return;
    }

    // Check for missaligned target address in 2-byte `ADR` and `LDR` instructions that only allow multiples of four.
    if (fixup.getTargetKind() == ARM::fixup_thumb_adr_pcrel_10 || fixup.getTargetKind() == ARM::fixup_arm_thumb_cp) {
        const auto& symbol = cast<const MCSymbolRefExpr>(fixup.getValue())->getSymbol();
        auto address = Layout.getFragmentOffset(symbol.getFragment()) + symbol.getOffset();

        // Check that target is 4-byte aligned
        if ((address & 3U) != 0) {
            context.reportError(fixup.getLoc(), "misaligned label address (reported by nyxstone)");
        }
    }

    // Check for out-of-bounds ARM Thumb2 `ADR` instruction
    if (fixup.getTargetKind() == ARM::fixup_t2_adr_pcrel_12) {
        auto offset = static_cast<int64_t>(cast<const MCSymbolRefExpr>(fixup.getValue())->getSymbol().getOffset());
        offset -= 4; // Source address is (PC + 4)

        // Check min/max bounds of instruction encoding
        // Symmetric bounds as `addw` and `subw` are used internally
        if (offset <= -4096 || offset >= 4096) {
            context.reportError(fixup.getLoc(), "out of range pc-relative fixup value (reported by Nyxstone)");
        }
    }

    // Check for misaligned target address for all ARM Thumb branch instructions
    if (fixup.getTargetKind() == ARM::fixup_arm_thumb_br || fixup.getTargetKind() == ARM::fixup_arm_thumb_bl
        || fixup.getTargetKind() == ARM::fixup_arm_thumb_bcc || fixup.getTargetKind() == ARM::fixup_t2_uncondbranch
        || fixup.getTargetKind() == ARM::fixup_t2_condbranch) {
        auto& symbol = cast<const MCSymbolRefExpr>(fixup.getValue())->getSymbol();
        auto address = Layout.getFragmentOffset(symbol.getFragment()) + symbol.getOffset();

        // Check that target is 2-byte aligned
        if ((address & 1) != 0) {
            context.reportError(fixup.getLoc(), "misaligned label address (reported by nyxstone)");
        }
    }

    // Check for out-of-bounds and misaligned label for ARM Thumb2 'LDC' instruction
    if (fixup.getTargetKind() == ARM::fixup_t2_pcrel_10) {
        auto& symbol = cast<const MCSymbolRefExpr>(fixup.getValue())->getSymbol();
        auto address = Layout.getFragmentOffset(symbol.getFragment()) + symbol.getOffset();

        auto offset = static_cast<int64_t>(symbol.getOffset()) - 4; // Source address is (PC + 4)

        // Since llvm only wrongly assembles for offsets which differ from the allowed value for delta < 4
        // it is enough to check that the offset is validly aligned to 4. For better error reporting,
        // we still check the offsets here.
        if (offset < -1020 || offset > 1020) {
            context.reportError(fixup.getLoc(), "out of range pc-relative fixup value (reported by Nyxstone)");
        }

        // check that target is 4 byte aligned
        if ((address & 3) != 0) {
            context.reportError(fixup.getLoc(), "misaligned label address (reported by Nyxstone)");
        }
    }
}

/// @brief Validates the given AARCH64 fixup with our custom validatinon routines.
///
/// @param fixup The fixup to be validated.
/// @param Layout The ASM Layout after fixups have been applied.
/// @param context The MCContext used to report errors.
void validate_aarch64(const MCFixup& fixup, [[maybe_unused]] const MCAsmLayout& Layout, MCContext& context)
{
    // Check for out-of-bounds AArch64 `ADR` instruction
    if (context.getTargetTriple().isAArch64() && fixup.getTargetKind() == AArch64::fixup_aarch64_pcrel_adr_imm21
        && fixup.getValue() != nullptr && fixup.getValue()->getKind() == MCExpr::Target
        && cast<const AArch64MCExpr>(fixup.getValue())->getSubExpr() != nullptr
        && cast<const AArch64MCExpr>(fixup.getValue())->getSubExpr()->getKind() == MCExpr::SymbolRef) {
        const auto* const sub_expr = cast<const AArch64MCExpr>(fixup.getValue())->getSubExpr();
        const auto offset = static_cast<int64_t>(cast<const MCSymbolRefExpr>(sub_expr)->getSymbol().getOffset());

        // Check min/max bounds of instruction encoding
        // Asymmetric bounds as two's complement is used
        if (offset < -0x100000 || offset >= 0x100000) {
            context.reportError(fixup.getLoc(), "fixup value out of range (reported by Nyxstone)");
        }
    }
}

void ObjectWriterWrapper::validate_fixups(const MCFragment& fragment, const MCAsmLayout& Layout)
{
    // Get fixups
    const SmallVectorImpl<MCFixup>* fixups = nullptr;
    switch (fragment.getKind()) {
    default:
        return;
    case MCFragment::FT_Data: {
        fixups = &cast<const MCDataFragment>(fragment).getFixups();
        break;
    }
    case MCFragment::FT_Relaxable: {
        fixups = &cast<const MCRelaxableFragment>(fragment).getFixups();
        break;
    }
    }

    // Iterate fixups
    for (const auto& fixup : *fixups) {
        // Additional validations for ARM Thumb instructions
        if (is_ArmT16_or_ArmT32(context.getTargetTriple())) {
            validate_arm_thumb(fixup, Layout, context);
        }

        // Additional validations for AArch64 instructions
        if (context.getTargetTriple().isAArch64()) {
            validate_aarch64(fixup, Layout, context);
        }
    }
}

void ObjectWriterWrapper::executePostLayoutBinding(llvm::MCAssembler& Asm, const llvm::MCAsmLayout& Layout)
{
    inner_object_writer->executePostLayoutBinding(Asm, Layout);
}

bool ObjectWriterWrapper::resolve_relocation(MCAssembler& assembler, const MCAsmLayout& layout,
    const MCFragment* fragment, const MCFixup& fixup, MCValue target, uint64_t& fixed_value)
{
    (void)layout;
    (void)fragment;
    (void)target;

    // LLVM performs relocation for the AArch64 instruction `adrp` during the linking step.
    // Therefore, we need to perform the relocation ourselves.
    // Semantics: REG := page of PC + page of .LABEL on 4k aligned page
    if (context.getTargetTriple().isAArch64()) {
        auto const IsPCRel
            = assembler.getBackend().getFixupKindInfo(fixup.getKind()).Flags & MCFixupKindInfo::FKF_IsPCRel;
        auto const kind = fixup.getTargetKind();

        if ((IsPCRel != 0U) && kind == AArch64::fixup_aarch64_pcrel_adrp_imm21) {
            const auto* const aarch64_expr = cast<AArch64MCExpr>(fixup.getValue());
            const auto* const symbol_ref = cast<MCSymbolRefExpr>(aarch64_expr->getSubExpr());
            MCSymbol const& symbol = symbol_ref->getSymbol();

            if (!symbol.isDefined()) {
                return false;
            }

            // Perform the fixup
            constexpr u64 PAGE_SIZE { 0x1000 };

            // We need to compute the absolute address of both this instruction and the target label,
            // so that we can compute the offsets of their two pages, since adrp zeros both
            // the lower 12 bits of the pc and of the offset...
            u64 local_addr = start_address + fixup.getOffset();
            u64 target_addr = start_address + symbol.getOffset();

            u64 local_page = local_addr & ~(PAGE_SIZE - 1);
            u64 target_page = target_addr & ~(PAGE_SIZE - 1);

            fixed_value = target_page - local_page;

            return true;
        }
    }

    return false;
}

// cppcheck-suppress unusedFunction
void ObjectWriterWrapper::recordRelocation(MCAssembler& Asm, const MCAsmLayout& Layout, const MCFragment* Fragment,
    const MCFixup& Fixup, MCValue Target, uint64_t& FixedValue)
{
    bool labels_defined { true };

    if (auto const* sym_ref = Target.getSymA(); sym_ref != nullptr) {
        labels_defined &= sym_ref->getSymbol().isDefined();
    }

    if (auto const* sym_ref = Target.getSymB(); sym_ref != nullptr) {
        labels_defined &= sym_ref->getSymbol().isDefined();
    }

    if (!labels_defined) {
        context.reportError(Fixup.getLoc(), "Label undefined (reported by Nyxstone)");
        return;
    }

    bool const resolved = resolve_relocation(Asm, Layout, Fragment, Fixup, Target, FixedValue);

    if (!resolved) {
        context.reportError(Fixup.getLoc(), "Could not resolve relocation/label (reported by Nyxstone)");
        return;
    }
}

uint64_t ObjectWriterWrapper::writeObject(MCAssembler& Asm, const MCAsmLayout& Layout)
{
    // If any label is undefined, continuing can lead to a segfault in the execution later.
    if (!extended_error.empty()) {
        return 0;
    }

    // Get .text section
    const auto& sections = Layout.getSectionOrder();
    const MCSection* const* text_section_it = std::find_if(
        std::begin(sections), std::end(sections), [](auto section) { return section->getName().str() == ".text"; });

    if (text_section_it == sections.end()) {
        extended_error += "[writeObject] Object has no .text section.";
        return 0;
    }

    const MCSection* text_section = *text_section_it;

    // Iterate fragments
    size_t curr_insn = 0;
    for (const MCFragment& fragment : *text_section) {
        // Additional validation of fixups that LLVM is missing
        validate_fixups(fragment, Layout);

        // If requested, do post processing of instruction details (that corrects for relocations and applied fixups)
        if (instructions == nullptr) {
            continue;
        }

        // Data fragment may contain multiple instructions that did not change in size
        if (fragment.getKind() == MCFragment::FT_Data) {
            const auto& data_fragment = cast<MCDataFragment>(fragment);
            const ArrayRef<char> contents = data_fragment.getContents();

            // Update bytes of multiple instructions
            size_t frag_pos = 0;
            while (true) {
                auto& insn_bytes = instructions->at(curr_insn).bytes;
                auto insn_len = insn_bytes.size();

                // Check if fragment contains this instruction
                if (frag_pos + insn_len > contents.size()) {
                    break;
                }

                // Update instruction bytes
                insn_bytes.clear();
                insn_bytes.reserve(insn_len);
                std::copy(contents.begin() + frag_pos, contents.begin() + frag_pos + insn_len,
                    std::back_inserter(insn_bytes));

                // Prepare next iteration
                frag_pos += insn_len;
                curr_insn++;

                // Check if more instructions to be updated exist
                if (curr_insn >= instructions->size()) {
                    break;
                }
            }
        } else if (fragment.getKind() == MCFragment::FT_Relaxable) {
            // Relaxable fragment contains exactly one instruction that may have increased in size
            const auto& relaxable_fragment = cast<MCRelaxableFragment>(fragment);
            const ArrayRef<char> contents = relaxable_fragment.getContents();

            // Update instruction bytes
            auto& insn_bytes = instructions->at(curr_insn).bytes;
            insn_bytes.clear();
            insn_bytes.reserve(contents.size());
            std::copy(contents.begin(), contents.end(), std::back_inserter(insn_bytes));

            // Prepare next iteration
            curr_insn++;
        }

        // Check if more instructions to be updated exist
        if (curr_insn >= instructions->size()) {
            break;
        }
    }

    // Generate output
    if (write_text_section_only) {
        // Write .text section bytes only
        const uint64_t start = stream.tell();
        Asm.writeSectionData(stream, text_section, Layout);
        return stream.tell() - start;
    }

    // Write complete object file
    return inner_object_writer->writeObject(Asm, Layout);
}

std::unique_ptr<MCObjectWriter> ObjectWriterWrapper::createObjectWriterWrapper(
    std::unique_ptr<MCObjectWriter>&& object_writer, raw_pwrite_stream& stream, MCContext& context,
    bool write_text_section_only, u64 start_address, std::string& extended_error,
    std::vector<Nyxstone::Instruction>* instructions)
{
    return std::make_unique<ObjectWriterWrapper>(std::move(object_writer), stream, context, write_text_section_only,
        start_address, extended_error, instructions);
}
} // namespace nyxstone
