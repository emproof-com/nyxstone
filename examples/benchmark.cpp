#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nyxstone.h"

using nyxstone::Nyxstone;
using nyxstone::NyxstoneBuilder;
using clock_type = std::chrono::steady_clock;

namespace {

struct ArchConfig {
    const char* name;
    const char* triple;
    std::vector<const char*> instructions;
};

const std::vector<ArchConfig> ARCHES {
    { "x86_64", "x86_64-linux-gnu",
        { "mov rax, rbx", "xor rax, rax", "add rsp, 8", "sub rsp, 8", "push rbx", "pop rbx", "inc rax", "dec rax",
            "and rax, rbx", "or rax, rbx" } },
    { "x86_32", "i686-linux-gnu",
        { "mov eax, ebx", "xor eax, eax", "add esp, 8", "sub esp, 8", "push ebx", "pop ebx", "inc eax", "dec eax",
            "and eax, ebx", "or eax, ebx" } },
    { "aarch64", "aarch64-linux-gnueabihf",
        { "mov x0, x1", "add x0, x0, #1", "sub x0, x0, #1", "mov x1, x2", "add x1, x1, #1", "sub x1, x1, #1",
            "mov x2, x3", "add sp, sp, #16", "sub sp, sp, #16", "ret" } },
    { "armv8m", "armv8m.main-none-eabi",
        { "mov r0, r1", "add r0, r0, #1", "sub r0, r0, #1", "mov r1, r2", "add r1, r1, #1", "sub r1, r1, #1",
            "mov r2, r3", "nop", "push {r0}", "pop {r0}" } },
};

constexpr std::array<size_t, 2> PACKAGE_SIZES { 1, 10 };

std::string make_package(const ArchConfig& arch, size_t package_size)
{
    std::string out;
    for (size_t i = 0; i < package_size; ++i) {
        if (i > 0) {
            out += "; ";
        }
        out += arch.instructions[i % arch.instructions.size()];
    }
    return out;
}

template <typename Fn> double run_for_at_least(double target_seconds, Fn&& fn)
{
    for (size_t i = 0; i < 10; ++i) {
        fn();
    }

    size_t count = 0;
    const auto start = clock_type::now();
    while (true) {
        for (size_t i = 0; i < 50; ++i) {
            fn();
            ++count;
        }
        const double elapsed = std::chrono::duration<double>(clock_type::now() - start).count();
        if (elapsed >= target_seconds) {
            return static_cast<double>(count) / elapsed;
        }
    }
}

std::string fmt_rate(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2);
    if (value >= 1e9) {
        stream << (value / 1e9) << " G";
    } else if (value >= 1e6) {
        stream << (value / 1e6) << " M";
    } else if (value >= 1e3) {
        stream << (value / 1e3) << " k";
    } else {
        stream << value << "  ";
    }
    std::string out = stream.str();
    while (out.size() < 10) {
        out.insert(out.begin(), ' ');
    }
    return out;
}

void run(const ArchConfig& arch, double seconds_per_bench)
{
    std::cout << "\n=== " << arch.name << " (" << arch.triple << ") ===\n";

    auto build_result = NyxstoneBuilder(std::string { arch.triple }).build();
    if (!build_result) {
        std::cerr << "  [skipped] " << build_result.error() << "\n";
        return;
    }
    const auto& nyxstone = build_result.value();

    for (size_t package_size : PACKAGE_SIZES) {
        const std::string assembly = make_package(arch, package_size);

        auto bytes_result = nyxstone->assemble(assembly, 0x1000, {});
        if (!bytes_result) {
            std::cerr << "  [skipped " << package_size << "] " << bytes_result.error() << "\n";
            continue;
        }
        const std::vector<uint8_t> bytes = bytes_result.value();

        auto instr_result = nyxstone->assemble_to_instructions(assembly, 0x1000, {});
        const size_t insn_count = instr_result ? instr_result.value().size() : package_size;

        const double asm_ops_per_s
            = run_for_at_least(seconds_per_bench, [&] { (void)nyxstone->assemble(assembly, 0x1000, {}); });
        const double dis_ops_per_s
            = run_for_at_least(seconds_per_bench, [&] { (void)nyxstone->disassemble(bytes, 0x1000, 0); });

        std::cout << "  Package " << std::setw(2) << package_size << " (" << insn_count << " insns, " << bytes.size()
                  << " bytes)\n"
                  << "    assemble    :" << fmt_rate(asm_ops_per_s) << " ops/s  "
                  << fmt_rate(asm_ops_per_s * static_cast<double>(insn_count)) << " insns/s  "
                  << fmt_rate(asm_ops_per_s * static_cast<double>(bytes.size())) << " bytes/s\n"
                  << "    disassemble :" << fmt_rate(dis_ops_per_s) << " ops/s  "
                  << fmt_rate(dis_ops_per_s * static_cast<double>(insn_count)) << " insns/s  "
                  << fmt_rate(dis_ops_per_s * static_cast<double>(bytes.size())) << " bytes/s\n";
    }
}

} // namespace

int main(int argc, char** argv)
{
    double seconds_per_bench = 0.5;
    if (argc > 1) {
        char* end = nullptr;
        const double parsed = std::strtod(argv[1], &end);
        if (end == argv[1] || parsed <= 0.0) {
            std::cerr << "Usage: " << argv[0] << " [seconds_per_measurement]\n";
            return 1;
        }
        seconds_per_bench = parsed;
    }

    std::cout << "Nyxstone benchmark [C++] (target " << seconds_per_bench << "s per measurement)";

    for (const auto& arch : ARCHES) {
        run(arch, seconds_per_bench);
    }

    return 0;
}
