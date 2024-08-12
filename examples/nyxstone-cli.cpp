#include <iomanip>
#include <iostream>
#include <optional>

#include <argh.h>
#include <expected.hpp>

#include "nyxstone.h"

using nyxstone::Nyxstone;
using nyxstone::NyxstoneBuilder;

std::vector<uint8_t> decode_instruction_bytes(std::string hex_string);
void print_bytes(const std::vector<uint8_t>& bytes);
std::optional<std::vector<Nyxstone::LabelDefinition>> parse_labels(std::string_view labelstr);
void print_instructions(const std::vector<Nyxstone::Instruction>& instructions);

constexpr auto USAGE = R"(Usage: nyxstone [-t=<triple>] [-p=<pc>] [-d] <input>

Examples:
  # Assemble an instruction with the default architecture ('x86_64').
  nyxstone 'push eax'

  # Disassemble the bytes 'ffc300d1' as AArch64 code.
  nyxstone -t aarch64 -d ffc300d1

Options:
  -t, --triple=<triple>      LLVM target triple or alias, e.g. 'aarch64'
  -c, --cpu=<cpu>            LLVM CPU specifier, e.g. 'cortex-a53'
  -f, --features=<list>      LLVM architecture/CPU feature list, e.g. '+mte,-neon'
  -p, --address=<pc>         Initial address to assemble/disassemble relative to
  -l, --labels=<list>        Label-to-address mappings (used when assembling only)
  -d, --disassemble          Treat <input> as bytes to disassemble instead of assembly
  -h, --help                 Show this help and usage message

Notes:
  The '--triple' parameter also supports aliases for common target triples:

     'x86_32' -> 'i686-linux-gnu'
     'x86_64' -> 'x86_64-linux-gnu'
     'armv6m' -> 'armv6m-none-eabi'
     'armv7m' -> 'armv7m-none-eabi'
     'armv8m' -> 'armv8m.main-none-eabi'
    'aarch64' -> 'aarch64-linux-gnueabihf'

  The CPUs for a target can be found with 'llc -mtriple=<triple> -mcpu=help'.
  The features for a target can be found with 'llc -mtriple=<triple> -mattr=help'.
)";

/// Parsed program options.
struct Options {
    std::string triple = "x86_64-linux-gnu";
    std::string cpu;
    std::string features;
    uint64_t address = 0;
    std::vector<Nyxstone::LabelDefinition> labels;
    bool disassemble = false;
    bool show_help = false;

    std::string input;

    static tl::expected<Options, std::string> parse(int argc, char const** argv);
};

tl::expected<Options, std::string> Options::parse(int argc, char const** argv)
{
    Options options;

    argh::parser args({
        "-t",
        "--target",
        "-c",
        "--cpu",
        "-f",
        "--features",
        "-p",
        "--address",
        "-l",
        "--labels",
    });
    args.parse(argc, argv);

    if (args[{ "-h", "--help" }]) {
        options.show_help = true;
        return options;
    }

    options.triple = args({ "-t", "--triple" }, /*default=*/"x86_64-linux-gnu").str();
    if (options.triple.empty()) {
        return tl::unexpected("Target triple not specified");
    }

    // These can both be empty as default options, so no need for a default value like above.
    options.cpu = args({ "-c", "--cpu" }).str();
    options.features = args({ "-f", "--features" }).str();

    std::string address_str = args({ "-p", "--address" }, /*default=*/"0").str();
    if (!address_str.empty()) {
        try {
            options.address = std::stoul(address_str, nullptr, 0);
        } catch (const std::exception&) {
            return tl::unexpected("Failed to parse address");
        }
    } else {
        return tl::unexpected("Address not specified");
    }

    auto labels_str = args({ "-l", "--labels" }).str();
    if (!labels_str.empty()) {
        auto parse_result = parse_labels(labels_str);
        if (!parse_result.has_value()) {
            return tl::unexpected("Failed to parse labels");
        }

        options.labels = parse_result.value();
    }

    options.disassemble = args[{ "-d", "--disassemble" }];

    if (args.pos_args().size() < 2) {
        return tl::unexpected("Missing input");
    }

    // TODO: Support multiple positional arguments.
    options.input = args[1];
    if (options.input.empty()) {
        return tl::unexpected("Input is empty");
    }

    return options;
}

int main(int argc, char const** argv)
{
    auto options_result = Options::parse(argc, argv);
    if (!options_result.has_value()) {
        std::cerr << "Error: " << options_result.error() << ".\n";
        std::cerr << "Hint: Try 'nyxstone -h' for help.\n";
        return 1;
    }

    auto options = options_result.value();
    if (options.show_help) {
        std::cout << USAGE;
        return 0;
    }

    auto builder = NyxstoneBuilder(std::move(options.triple))
                       .with_cpu(std::move(options.cpu))
                       .with_features(std::move(options.features));
    auto build_result = std::move(builder.build());
    if (!build_result) {
        std::cerr << "Error: Failed to create Nyxstone instance (" << build_result.error() << ")\n";
        return 1;
    }
    auto nyxstone = std::move(build_result.value());

    if (options.disassemble) {
        auto bytes = decode_instruction_bytes(options.input);
        if (bytes.empty()) {
            std::cerr << "Error: Failed to decode bytes as hex.\n";
            return 1;
        }

        nyxstone->disassemble_to_instructions(bytes, options.address, 0)
            .map_error([&bytes](const auto& error) {
                std::cerr << "Could not disassemble ";
                print_bytes(bytes);
                std::cerr << " (" << error << ")\n";
                exit(1);
            })
            .map(print_instructions);
    } else {
        const auto& assembly = options.input;
        nyxstone->assemble_to_instructions(assembly, options.address, options.labels)
            .map_error([&assembly](const auto& error) {
                std::cerr << "Could not assemble " << assembly << " (" << error << ")\n";
                exit(1);
            })
            .map(print_instructions);
    }
}

std::vector<uint8_t> decode_instruction_bytes(std::string hex_string)
{
    // Drop all spaces first to support round-tripping of Nyxstone output as input.
    hex_string.erase(std::remove_if(hex_string.begin(), hex_string.end(), isspace), hex_string.end());

    auto input_size = hex_string.size();
    if (input_size % 2 != 0) {
        return {};
    }

    try {
        std::vector<uint8_t> result;
        result.reserve(input_size / 2);
        for (size_t i = 0; i < input_size; i += 2) {
            result.emplace_back(std::stoul(hex_string.substr(i, 2), nullptr, 16));
        }

        return result;
    } catch (...) {
        return {};
    }
}

void print_address(uint64_t address)
{
    std::cout << "\t0x" << std::hex << std::setfill('0') << std::right << std::setw(8) << address;
}

void print_instructions(const std::vector<Nyxstone::Instruction>& instructions)
{
    for (const auto& instr : instructions) {
        print_address(instr.address);
        std::cout << ": " << std::setfill(' ') << std::left << std::setw(32) << instr.assembly;
        print_bytes(instr.bytes);
        std::cout << "\n";
    }
}

void print_bytes(const std::vector<uint8_t>& bytes)
{
    std::cout << std::hex << " ; ";
    for (const auto& byte : bytes) {
        std::cout << std::setfill('0') << std::setw(2) << static_cast<uint32_t>(byte) << " ";
    }
    std::cout << std::dec;
}

std::optional<std::vector<Nyxstone::LabelDefinition>> parse_labels(std::string_view labelstr)
{
    constexpr std::string_view allowed_label_chars {
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."
    };
    constexpr std::string_view not_allowed_first_char { "01234567890-" };

    auto delim = ',';

    std::vector<Nyxstone::LabelDefinition> labels;

    if (labelstr.empty()) {
        return labels;
    }

    auto remaing_unparsed = labelstr.substr();

    while (!remaing_unparsed.empty()) {
        auto delim_pos = remaing_unparsed.find(delim);

        auto token = remaing_unparsed.substr(0, delim_pos);

        auto assignment_delim = token.find('=');
        if (assignment_delim == std::string::npos) {
            std::cerr << "No `=` in label assignment found: " << token << "\n";
            return {};
        }

        auto const name = token.substr(0, assignment_delim);
        auto const val = token.substr(assignment_delim + 1);

        if (name.find_first_not_of(allowed_label_chars) != std::string::npos
            || not_allowed_first_char.find_first_of(name.front()) != std::string::npos) {
            std::cerr << "Invalid label name: `" << token << "`\n";
            return {};
        }

        uint64_t value = 0;
        try {
            char* end { nullptr };
            value = std::strtoul(val.data(), &end, 0);

            // Failing to parse the value to the end or an exception mean we failed
            // to parse the value.
            if (end != val.data() + val.size()) {
                std::cerr << "Could not parse label value: `" << val << "`\n";
                return {};
            }
        } catch (const std::exception& e) {
            std::cerr << "Could not parse label value: `" << val << "` (" << e.what() << ")\n";
            return {};
        }

        labels.push_back(Nyxstone::LabelDefinition { std::string(name), value });

        auto const next_token_or_end = (delim_pos != std::string::npos) ? delim_pos + 1 : remaing_unparsed.size();
        remaing_unparsed = remaing_unparsed.substr(next_token_or_end);
    }

    return labels;
}
