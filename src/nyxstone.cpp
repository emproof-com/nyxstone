#include "nyxstone.h"

#include "ELFStreamerWrapper.h"
#include "ObjectWriterWrapper.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmBackend.h>
#include <llvm/MC/MCCodeEmitter.h>
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCParser/MCAsmParser.h>
#include <llvm/MC/MCParser/MCTargetAsmParser.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#pragma GCC diagnostic pop

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

    return std::make_unique<Nyxstone>(std::move(triple),
        // target is a static object, thus it is safe to take its reference here:
        *target, std::move(target_options), std::move(register_info), std::move(assembler_info),
        std::move(instruction_info), std::move(subtarget_info), std::move(instruction_printer));
}

tl::expected<std::vector<u8>, std::string> Nyxstone::assemble(
    const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const
{
    std::vector<u8> bytes;
    return assemble_impl(assembly, address, labels, bytes, nullptr).transform([bytes]() { return bytes; });
}

tl::expected<std::vector<Nyxstone::Instruction>, std::string> Nyxstone::assemble_to_instructions(
    const std::string& assembly, uint64_t address, const std::vector<LabelDefinition>& labels) const
{
    std::vector<Instruction> instructions;
    std::vector<u8> output_bytes;
    return assemble_impl(assembly, address, labels, output_bytes, &instructions)
        .and_then([instructions, &output_bytes]() -> tl::expected<std::vector<Instruction>, std::string> {
            // Pedantic: Ensure accumulated instruction byte length matches output byte length
            // This also leads to nyxstone not supporting directives which insert data into the assembly,
            // since the bytes will not match the assembled instructions.
            const size_t insn_byte_length = std::accumulate(instructions.begin(), instructions.end(),
                static_cast<size_t>(0), [](size_t acc, const Instruction& insn) { return acc + insn.bytes.size(); });
            if (insn_byte_length != output_bytes.size()) {
                std::stringstream error_stream;
                error_stream << "Internal error (= insn_byte_length '" << insn_byte_length << "' != output_bytes.size "
                             << output_bytes.size() << ")";
                return tl::unexpected(error_stream.str());
            }

            return instructions;
        });
}

tl::expected<std::string, std::string> Nyxstone::disassemble(
    const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const
{
    std::string disassembly;
    return disassemble_impl(bytes, address, count, &disassembly, nullptr).transform([disassembly]() {
        return disassembly;
    });
}

tl::expected<std::vector<Nyxstone::Instruction>, std::string> Nyxstone::disassemble_to_instructions(
    const std::vector<uint8_t>& bytes, uint64_t address, size_t count) const
{
    std::vector<Nyxstone::Instruction> instructions;
    return disassemble_impl(bytes, address, count, nullptr, &instructions).transform([instructions]() {
        return instructions;
    });
}

namespace {
    const char* const PREPENDED_ASSEMBLY { "bkpt #0x42\n" };
    constexpr std::array<uint8_t, 2> PREPENDED_BYTES { 0x42, 0xbe };

    std::string prepend_bkpt(std::string&& assembly, bool needs_prepend)
    {
        if (needs_prepend) {
            assembly.insert(0, PREPENDED_ASSEMBLY);
        }

        return assembly;
    }

    tl::expected<llvm::SmallVector<char, 128>, std::string> remove_bkpt(
        llvm::SmallVector<char, 128> bytes, std::vector<Nyxstone::Instruction>* instructions, bool has_prepend)
    {
        // If bkpt got prepended for alignment reasons, remove it from instructions
        // and bytes
        if (has_prepend) {
            if (instructions != nullptr) {
                const auto& first_instr_bytes = instructions->front().bytes;
                const bool has_prepended_bytes = !instructions->empty() && first_instr_bytes.size() == 2
                    && std::equal(begin(first_instr_bytes), end(first_instr_bytes), begin(PREPENDED_BYTES));
                if (!has_prepended_bytes) {
                    return tl::unexpected("Did not find prepended bkpt at first instruction.");
                }
                instructions->erase(instructions->begin());
            }

            if (bytes.size() < 2 || memcmp(PREPENDED_BYTES.data(), bytes.data(), 2) != 0) {
                std::ostringstream error_stream;
                error_stream << "Did not find prepended bkpt at first two bytes.";
                error_stream << " Found bytes 0x" << std::hex << static_cast<unsigned int>(bytes[0]) << " 0x"
                             << static_cast<unsigned int>(bytes[1]);
                return tl::unexpected(error_stream.str());
            }
            bytes.erase(bytes.begin(), bytes.begin() + 2);
        }

        return bytes;
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

    // ARM Thumb has mixed 2-byte and 4-byte instructions. The base address used
    // for branch/load/store offset calculations is aligned down to the last
    // 4-byte boundary `base = Align(PC, 4)`. LLVM always assembles at address
    // zero and external label definitions are adjusted accordingly. This
    // combination leads to alignment issues resulting in wrong instruction bytes.
    // Hence, for ARM Thumb Nyxstone prepends 2 bytes for 2-byte aligned, but not
    // 4-byte aligned, start addresses to create the correct alignment behavior.
    // In the label offset computation Nyxstone compensates for these extra 2
    // bytes. The breakpoint instruction `bkpt #0x42` was chosen as prepend
    // mechanism as it only has a 2-byte encoding on ARMv6/7/8-M and is an unusual
    // instruction. The prepended breakpoint instruction gets removed by Nyxstone
    // in output `bytes` and `instructions` to have a clean result.
    //
    // tl;dr
    // For Thumb prepend 2 byte in assembly if `(address % 4) == 2` to get correct
    // alignment behavior.
    const bool needs_prepend { is_ArmT16_or_ArmT32(triple) && address % 4 == 2 };
    const std::string input_assembly { prepend_bkpt(std::string { assembly }, needs_prepend) };

    // Add input assembly text
    llvm::SourceMgr source_manager;
    source_manager.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBuffer(input_assembly), llvm::SMLoc());

    std::string extended_error;

    // Equip context with info objects and custom error handling
    llvm::MCContext context(
        triple, assembler_info.get(), register_info.get(), subtarget_info.get(), &source_manager, &target_options);
    context.setDiagnosticHandler([&extended_error](const llvm::SMDiagnostic& SMD, bool IsInlineAsm,
                                     const llvm::SourceMgr& SrcMgr, std::vector<const llvm::MDNode*> const& LocInfos) {
        // Suppress unused parameter warning
        (void)IsInlineAsm;
        (void)SrcMgr;
        (void)LocInfos;

        llvm::SmallString<128> error_msg;

        llvm::raw_svector_ostream error_stream(error_msg);
        SMD.print(nullptr, error_stream, /* ShowColors */ false);
        extended_error += error_msg.c_str();
    });

    auto object_file_info
        = std::unique_ptr<llvm::MCObjectFileInfo>(target.createMCObjectFileInfo(context, /*PIC=*/false));
    if (!object_file_info) {
        return tl::unexpected("Could not create LLVM object (= MCObjectFileInfo )");
    }
    context.setObjectFileInfo(object_file_info.get());

    // Create code emitter (for llvm 15)
    auto code_emitter = std::unique_ptr<llvm::MCCodeEmitter>(target.createMCCodeEmitter(*instruction_info, context));
    if (!code_emitter) {
        return tl::unexpected("Could not create LLVM object (= MCCodeEmitter )");
    }

    // Create assembler backend
    auto assembler_backend = std::unique_ptr<llvm::MCAsmBackend>(
        target.createMCAsmBackend(*subtarget_info, *register_info, target_options));
    if (!assembler_backend) {
        return tl::unexpected("Could not create LLVM object (= MCAsmBackend )");
    }

    // Create object writer and object writer wrapper (that handles custom fixup
    // and output handling)
    llvm::SmallVector<char, 128> output_bytes;
    llvm::raw_svector_ostream stream(output_bytes);
    auto object_writer = assembler_backend->createObjectWriter(stream);
    auto object_writer_wrapper
        = ObjectWriterWrapper::createObjectWriterWrapper(std::move(object_writer), stream, context,
            /* write_text_section_only */ true, address, extended_error, instructions);
    if (!object_writer_wrapper) {
        return tl::unexpected("Could not create LLVM object (= ObjectWriterWrapper )");
    }

    // Create object streamer and object streamer wrapper (that records
    // instruction details)
    if (!triple.isOSBinFormatELF()) {
        std::stringstream error_stream;
        error_stream << "ELF does not support target triple '" << triple.getTriple() << "'.";
        return tl::unexpected(error_stream.str());
    }
    auto streamer = ELFStreamerWrapper::createELFStreamerWrapper(context, std::move(assembler_backend),
        std::move(object_writer_wrapper), std::move(code_emitter),
        /* RelaxAll */ false, instructions, extended_error, *instruction_printer);
    streamer->setUseAssemblerInfoForParsing(true);

    // Create assembly parser and target specific assembly parser
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

    // Initialize .text section
    streamer->initSections(false, parser->getTargetParser().getSTI());

    // Search first data fragment
    auto& fragments = *streamer->getCurrentSectionOnly();
    auto fragment_it = std::find_if(fragments.begin(), fragments.end(),
        [](auto& fragment) { return fragment.getKind() == llvm::MCFragment::FT_Data; });
    if (fragment_it == std::end(fragments)) {
        return tl::unexpected("Could not find initial data fragment.");
    }

    // Inject user-defined labels
    const uint64_t compensate_prepended_bkpt = (needs_prepend) ? PREPENDED_BYTES.size() : 0u;
    for (const auto& label : labels) {
        auto* inj_symbol = context.getOrCreateSymbol(label.name);
        inj_symbol->setOffset(label.address - address + compensate_prepended_bkpt);
        inj_symbol->setFragment(&*fragment_it);
    }

    // Perform assembly process
    const bool error = parser->Run(/* NoInitialTextSection= */ true);
    if (error || !extended_error.empty()) {
        std::ostringstream error_stream;
        error_stream << "Error during assembly";
        if (!extended_error.empty()) {
            error_stream << ": " << extended_error;
        }
        return tl::unexpected(error_stream.str());
    }

    auto res = remove_bkpt(output_bytes, instructions, needs_prepend).transform([&bytes](const auto& output) -> void {
        // Copy bytes to output
        bytes.clear();
        bytes.reserve(output.size());
        std::copy(output.begin(), output.end(), std::back_inserter(bytes));
    });

    // Assign addresses if instruction details requested
    if (instructions != nullptr) {
        uint64_t current_address = address;
        for (Nyxstone::Instruction& insn : *instructions) {
            insn.address = current_address;
            current_address += insn.bytes.size();
        }
    }

    return res;
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

    // Equip context with info objects and custom error handling
    llvm::SmallString<128> error_msg;
    llvm::MCContext context(
        triple, assembler_info.get(), register_info.get(), subtarget_info.get(), nullptr, &target_options);
    context.setDiagnosticHandler([&error_msg](const llvm::SMDiagnostic& SMD, bool IsInlineAsm,
                                     const llvm::SourceMgr& SrcMgr, std::vector<const llvm::MDNode*> const& LocInfos) {
        // Suppress unused parameter warning
        (void)IsInlineAsm;
        (void)SrcMgr;
        (void)LocInfos;

        llvm::raw_svector_ostream error_stream(error_msg);
        SMD.print(nullptr, error_stream, /* ShowColors */ false);
    });

    // Create disassembler
    auto disassembler = std::unique_ptr<llvm::MCDisassembler>(target.createMCDisassembler(*subtarget_info, context));
    if (!disassembler) {
        return tl::unexpected("Invalid architecture / LLVM target triple");
    }

    // Disassemble
    const llvm::ArrayRef<u8> data(bytes.data(), bytes.size());
    uint64_t pos = 0;
    uint64_t insn_count = 0;
    while (true) {
        // Decompose one instruction
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

        // Generate instruction disassembly text
        std::string insn_str;
        llvm::raw_string_ostream str_stream(insn_str);
        instruction_printer->printInst(&insn,
            /* Address */ address + pos,
            /* Annot */ "", *subtarget_info, str_stream);

        // left trim
        insn_str.erase(0, insn_str.find_first_not_of(" \t\n\r"));
        // convert tabulators to spaces
        std::replace(insn_str.begin(), insn_str.end(), '\t', ' ');

        // Add instruction to results
        if (disassembly != nullptr) {
            *disassembly += insn_str + "\n";
        }
        if (instructions != nullptr) {
            Nyxstone::Instruction new_insn;
            new_insn.address = address + pos;
            new_insn.assembly = insn_str;
            new_insn.bytes.reserve(insn_size);
            std::copy(data.begin() + pos, data.begin() + pos + insn_size, std::back_inserter(new_insn.bytes));
            instructions->push_back(new_insn);
        }

        // Abort after n instructions if requested
        insn_count += 1;
        if (count != 0 && insn_count >= count) {
            break;
        }

        // Prepare next iteration
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
