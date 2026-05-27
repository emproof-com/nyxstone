//! Cross-language counterpart to `examples/benchmark.cpp`.
//!
//! Same architectures, same instructions, same package sizes (1 and 10), same
//! measurement methodology and output layout — so output from this binary can
//! be compared directly against the C++ benchmark (e.g. `diff` after dropping
//! the `[C++]` / `[Rust]` tag on the first line).
//!
//! Run with: `cargo run --release --example benchmark -- [seconds_per_measurement]`

extern crate anyhow;
extern crate nyxstone;

use std::env;
use std::time::{Duration, Instant};

use anyhow::Result;
use nyxstone::{Nyxstone, NyxstoneConfig};

struct ArchConfig {
    name: &'static str,
    triple: &'static str,
    instructions: &'static [&'static str],
}

const ARCHES: &[ArchConfig] = &[
    ArchConfig {
        name: "x86_64",
        triple: "x86_64-linux-gnu",
        instructions: &[
            "mov rax, rbx",
            "xor rax, rax",
            "add rsp, 8",
            "sub rsp, 8",
            "push rbx",
            "pop rbx",
            "inc rax",
            "dec rax",
            "and rax, rbx",
            "or rax, rbx",
        ],
    },
    ArchConfig {
        name: "x86_32",
        triple: "i686-linux-gnu",
        instructions: &[
            "mov eax, ebx",
            "xor eax, eax",
            "add esp, 8",
            "sub esp, 8",
            "push ebx",
            "pop ebx",
            "inc eax",
            "dec eax",
            "and eax, ebx",
            "or eax, ebx",
        ],
    },
    ArchConfig {
        name: "aarch64",
        triple: "aarch64-linux-gnueabihf",
        instructions: &[
            "mov x0, x1",
            "add x0, x0, #1",
            "sub x0, x0, #1",
            "mov x1, x2",
            "add x1, x1, #1",
            "sub x1, x1, #1",
            "mov x2, x3",
            "add sp, sp, #16",
            "sub sp, sp, #16",
            "ret",
        ],
    },
    ArchConfig {
        name: "armv8m",
        triple: "armv8m.main-none-eabi",
        instructions: &[
            "mov r0, r1",
            "add r0, r0, #1",
            "sub r0, r0, #1",
            "mov r1, r2",
            "add r1, r1, #1",
            "sub r1, r1, #1",
            "mov r2, r3",
            "nop",
            "push {r0}",
            "pop {r0}",
        ],
    },
];

const PACKAGE_SIZES: &[usize] = &[1, 10];

fn make_package(arch: &ArchConfig, package_size: usize) -> String {
    let mut out = String::new();
    for i in 0..package_size {
        if i > 0 {
            out.push_str("; ");
        }
        out.push_str(arch.instructions[i % arch.instructions.len()]);
    }
    out
}

fn run_for_at_least<F: FnMut()>(target: Duration, mut f: F) -> f64 {
    for _ in 0..10 {
        f();
    }

    let mut count: usize = 0;
    let start = Instant::now();
    loop {
        for _ in 0..50 {
            f();
            count += 1;
        }
        let elapsed = start.elapsed();
        if elapsed >= target {
            return count as f64 / elapsed.as_secs_f64();
        }
    }
}

fn fmt_rate(value: f64) -> String {
    let body = if value >= 1e9 {
        format!("{:.2} G", value / 1e9)
    } else if value >= 1e6 {
        format!("{:.2} M", value / 1e6)
    } else if value >= 1e3 {
        format!("{:.2} k", value / 1e3)
    } else {
        format!("{:.2}  ", value)
    };
    format!("{:>10}", body)
}

fn run(arch: &ArchConfig, target: Duration) {
    println!("\n=== {} ({}) ===", arch.name, arch.triple);

    let nyxstone = match Nyxstone::new(arch.triple, NyxstoneConfig::default()) {
        Ok(n) => n,
        Err(e) => {
            eprintln!("  [skipped] {e}");
            return;
        }
    };

    for &package_size in PACKAGE_SIZES {
        let assembly = make_package(arch, package_size);

        let bytes = match nyxstone.assemble(&assembly, 0x1000) {
            Ok(b) => b,
            Err(e) => {
                eprintln!("  [skipped {package_size}] {e}");
                continue;
            }
        };

        let insn_count = nyxstone
            .assemble_to_instructions(&assembly, 0x1000)
            .map(|v| v.len())
            .unwrap_or(package_size);

        let asm_ops_per_s = run_for_at_least(target, || {
            let _ = nyxstone.assemble(&assembly, 0x1000);
        });
        let dis_ops_per_s = run_for_at_least(target, || {
            let _ = nyxstone.disassemble(&bytes, 0x1000, 0);
        });

        println!(
            "  Package {:>2} ({} insns, {} bytes)",
            package_size,
            insn_count,
            bytes.len()
        );
        println!(
            "    assemble    :{} ops/s  {} insns/s  {} bytes/s",
            fmt_rate(asm_ops_per_s),
            fmt_rate(asm_ops_per_s * insn_count as f64),
            fmt_rate(asm_ops_per_s * bytes.len() as f64),
        );
        println!(
            "    disassemble :{} ops/s  {} insns/s  {} bytes/s",
            fmt_rate(dis_ops_per_s),
            fmt_rate(dis_ops_per_s * insn_count as f64),
            fmt_rate(dis_ops_per_s * bytes.len() as f64),
        );
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = env::args().collect();
    let mut seconds_per_bench: f64 = 0.5;
    if args.len() > 1 {
        match args[1].parse::<f64>() {
            Ok(v) if v > 0.0 => seconds_per_bench = v,
            _ => {
                eprintln!("Usage: {} [seconds_per_measurement]", args[0]);
                std::process::exit(1);
            }
        }
    }
    let target = Duration::from_secs_f64(seconds_per_bench);

    print!("Nyxstone benchmark [Rust] (target {seconds_per_bench}s per measurement)");

    for arch in ARCHES {
        run(arch, target);
    }

    Ok(())
}
