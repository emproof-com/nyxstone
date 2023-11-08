#include <boost/algorithm/hex.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
#include <unordered_map>

#include "nyxstone.h"
#include "tl/expected.hpp"

namespace po = boost::program_options;

using nyxstone::Nyxstone;
using nyxstone::NyxstoneBuilder;

enum class Architecture;
std::optional<Architecture> arch_parse_from_string(const std::string& arch);
void print_bytes(const std::vector<uint8_t>& bytes);
std::string arch_to_llvm_string(Architecture arch);
std::optional<std::vector<Nyxstone::LabelDefinition>> parse_labels(std::string labelstr);
void print_instructions(const std::vector<Nyxstone::Instruction>& instructions);

int main(int argc, char** argv)
{
    // clang-format off
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "Show this message")
        ("arch", po::value<std::string>()->default_value("x86_64"),
            R"(LLVM triple or architecture identifier of triple, for example "x86_64", "x86_64-linux-gnu", "armv8", "armv8eb", "thumbv8", "aarch64")")
        ("address", po::value<std::string>()->default_value("0"), "Address")
    ;

    po::options_description desc_asm("Assembling");
    desc_asm.add_options()
        ("labels", po::value<std::string>(), "Labels, for example \"label0=0x10,label1=0x20\"")
        ("assemble,A", po::value<std::string>(), "Assembly")
    ;

    po::options_description desc_disasm("Disassembling");
    desc_disasm.add_options()
        ("disassemble,D", po::value<std::string>(), "Byte code in hex, for example: \"0203\"")
    ;
    // clang-format on

    desc.add(desc_asm);
    desc.add(desc_disasm);

    po::variables_map varmap;
    try {
        po::store(po::parse_command_line(argc, argv, desc), varmap);
    } catch (const std::exception& e) {
        std::cerr << "Error occured: " << e.what() << "\n";
        return 2;
    }
    po::notify(varmap);

    if (varmap.empty() || varmap.count("help") != 0) {
        std::cout << desc << "\n";
        return 0;
    }

    const bool has_assemble = varmap.count("assemble") > 0;
    const bool has_disassemble = varmap.count("disassemble") > 0;

    if (!has_assemble && !has_disassemble) {
        std::cout << "Nothing to do\n";
        std::cout << desc << "\n";
        return 1;
    }

    if (has_assemble && has_disassemble) {
        std::cout << "Choose one of assemble/disassemble\n";
        std::cout << desc << "\n";
        return 1;
    }

    auto arch = varmap["arch"].as<std::string>();

    auto has_labels = varmap.count("labels") > 0;
    if (has_labels && has_disassemble) {
        std::cout << "Invalid argument\n";
        std::cout << desc << "\n";
        return 1;
    }

    std::vector<Nyxstone::LabelDefinition> labels = {};
    if (has_labels && has_assemble) {
        auto maybe_labels = parse_labels(varmap["labels"].as<std::string>());
        if (!maybe_labels.has_value()) {
            return 1;
        }
        labels = maybe_labels.value();
    }

    uint64_t address = 0;
    auto addr = varmap["address"].as<std::string>();
    try {
        address = std::stoul(addr, nullptr, 0);
    } catch (const std::exception&) {
        std::cerr << "Could not parse address\n";
        return 1;
    }

    auto nyxstone_result { std::move(NyxstoneBuilder().with_triple(std::move(arch)).build()) };
    if (!nyxstone_result) {
        std::cerr << "Failure creating nyxstone instance (= " << nyxstone_result.error() << " )\n";
        return 1;
    }
    auto nyxstone { std::move(nyxstone_result.value()) };

    if (has_assemble) {
        const auto& assembly = varmap["assemble"].as<std::string>();
        nyxstone->assemble_to_instructions(assembly, address, labels)
            .map_error([&assembly](const auto& error) {
                std::cerr << "Could not assemble " << assembly << " (= " << error << " )\n";
                exit(1);
            })
            .map(print_instructions);
    }

    if (has_disassemble) {
        std::vector<uint8_t> bytes {};
        auto byte_code = varmap["disassemble"].as<std::string>();
        byte_code.erase(std::remove_if(byte_code.begin(), byte_code.end(), ::isspace), byte_code.end());
        boost::algorithm::unhex(byte_code.begin(), byte_code.end(), std::back_inserter(bytes));

        nyxstone->disassemble_to_instructions(bytes, address, 0)
            .map_error([&bytes](const auto& error) {
                std::cerr << "Could not disassemble ";
                print_bytes(bytes);
                std::cerr << " (= " << error << " )\n";
                exit(1);
            })
            .map(print_instructions);
    }

    return 0;
}

void print_instructions(const std::vector<Nyxstone::Instruction>& instructions)
{
    for (const auto& instr : instructions) {
        std::cout << "\t0x" << std::hex << std::setfill('0') << std::setw(8) << instr.address << ": " << instr.assembly
                  << " - ";
        print_bytes(instr.bytes);
        std::cout << "\n";
    }
}

void print_bytes(const std::vector<uint8_t>& bytes)
{
    std::cout << std::hex << "[ ";
    for (const auto& byte : bytes) {
        std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
    }
    std::cout << std::dec << "]";
}

std::optional<std::vector<Nyxstone::LabelDefinition>> parse_labels(std::string labelstr)
{
    auto delim = ',';

    std::vector<Nyxstone::LabelDefinition> labels;

    while (!labelstr.empty()) {
        auto delim_pos = labelstr.find(delim);
        auto token = labelstr.substr(0, delim_pos);
        if (token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-=")
            == std::string::npos) {
            std::cerr << "Invalid label: " << token << "\n";
            return {};
        }

        auto assignment_delim = token.find('=');
        if (assignment_delim == std::string::npos) {
            std::cerr << "Invalid label: " << token << "\n";
            return {};
        }

        auto name = token.substr(0, labelstr.find('='));
        auto val = token.substr(labelstr.find('=') + 1);

        uint64_t value = 0;
        try {
            value = std::stoul(val, nullptr, 0);
        } catch (const std::exception& e) {
            std::cerr << "Could not parse label value: `" << val << "`\n";
            return {};
        }

        labels.push_back(Nyxstone::LabelDefinition { name, value });

        labelstr.erase(0, delim_pos == std::string::npos ? delim_pos : delim_pos + 1);
    }

    return labels;
}
