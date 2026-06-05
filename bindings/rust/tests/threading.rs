//! Multi-threaded stress tests for `nyxstone`.
//!
//! Strategy: collect a single-threaded golden reference, then hammer the same
//! inputs from many threads — each with its own `Nyxstone` instance (since
//! `Nyxstone: !Sync`) — and assert every multi-threaded result is byte/string
//! identical to the golden reference.
//!
//! The tests are designed to expose:
//!   - shared LLVM-MC state races between independent Nyxstone instances
//!     (target registry, MCContext defaults, code-emitter tables, etc.)
//!   - lock-acquisition races during construction (the static
//!     `build_common_mutex` in NyxstoneBuilder::build)
//!   - corruption visible as wrong-but-not-erroring output bytes
//!   - sporadic errors that "shouldn't happen" — gas/LLVM diagnostics
//!     leaking across instances.
//!
//! Default runtime is ~8 s so `cargo test --release` completes quickly.
//! Set NYXSTONE_STRESS_SECS=30 (or any positive integer) for a longer soak.
//!
//! Run with:
//!   cargo test --release --test threading -- --test-threads=1 --nocapture
//!
//! `--test-threads=1` is intentional: each test function spawns its own
//! thread pool internally, and Cargo's own test parallelism would multiply
//! the load unevenly and obscure attribution.

use nyxstone::{Instruction, Nyxstone, NyxstoneConfig};
use std::sync::{Arc, Barrier};
use std::thread;
use std::time::Duration;

/// Returns the per-test soak duration.  Defaults to 2 s so the full suite
/// finishes in ~8 s under `cargo test --release`.  Set NYXSTONE_STRESS_SECS
/// to a positive integer for a longer soak run.
fn stress_duration() -> Duration {
    let secs = std::env::var("NYXSTONE_STRESS_SECS")
        .ok()
        .and_then(|s| s.parse::<u64>().ok())
        .filter(|&n| n > 0)
        .unwrap_or(2);
    Duration::from_secs(secs)
}

// ---- helpers ------------------------------------------------------------

#[derive(Clone, Debug)]
struct Job {
    triple: &'static str,
    asm:    &'static str,
    addr:   u64,
}

/// Workload spanning the architectures most users hit.  Each arch contributes
/// a few representative instructions so the test exercises different MC
/// backends in the same thread pool.
fn workload() -> Vec<Job> {
    let mut v = Vec::new();
    for &(triple, asms) in &[
        ("x86_64-linux-gnu", &[
            "mov rax, rbx",
            "add rax, 1",
            "nop",
            "ret",
            "call 0x1000",
            "mov rax, qword ptr [rdi + 0x10]",
        ][..]),
        ("i686-linux-gnu", &[
            "mov eax, ebx",
            "add eax, 1",
            "nop",
            "ret",
            "push ebp",
        ][..]),
        ("aarch64-linux-gnueabihf", &[
            "mov x0, x1",
            "add x0, x1, x2",
            "nop",
            "ret",
            "ldr x0, [x1]",
        ][..]),
        ("armv7m-none-eabi", &[
            "mov r0, r1",
            "add r0, r1, r2",
            "nop",
            "bx lr",
        ][..]),
    ] {
        for (i, asm) in asms.iter().enumerate() {
            v.push(Job { triple, asm, addr: 0x1000 + (i as u64) * 0x10 });
        }
    }
    v
}

/// Run the workload single-threaded to capture the reference output.  Build
/// one Nyxstone per *arch* (matching how real users batch by arch).
fn golden_assemble(workload: &[Job]) -> Vec<(Job, Result<Vec<u8>, String>)> {
    let mut out = Vec::with_capacity(workload.len());
    for job in workload {
        let nx = Nyxstone::new(job.triple, NyxstoneConfig::default())
            .map_err(|e| format!("Nyxstone::new({}): {e}", job.triple));
        let result = nx.and_then(|n| {
            n.assemble(job.asm, job.addr).map_err(|e| e.to_string())
        });
        out.push((job.clone(), result));
    }
    out
}

fn golden_assemble_to_insns(workload: &[Job]) -> Vec<(Job, Result<Vec<Instruction>, String>)> {
    let mut out = Vec::with_capacity(workload.len());
    for job in workload {
        let nx = Nyxstone::new(job.triple, NyxstoneConfig::default())
            .map_err(|e| format!("Nyxstone::new({}): {e}", job.triple));
        let result = nx.and_then(|n| {
            n.assemble_to_instructions(job.asm, job.addr).map_err(|e| e.to_string())
        });
        out.push((job.clone(), result));
    }
    out
}

/// Format a diff between the expected and actual outcome of one job.
fn fmt_diff<T: std::fmt::Debug + PartialEq>(
    job: &Job, expected: &Result<T, String>, actual: &Result<T, String>,
) -> String {
    format!(
        "DIFF [{}] {:?}@{:#x}\n  expected: {:?}\n  actual:   {:?}",
        job.triple, job.asm, job.addr, expected, actual
    )
}

// ---- stress harness -----------------------------------------------------

/// Run `op` from `n_threads` threads, each looping `iters` times over the
/// workload.  Threads start in lockstep via a `Barrier` so the contention
/// window is maximised.  Any deviation from the golden reference is
/// collected and returned.
fn stress_assemble(
    workload: &[Job],
    golden: &[(Job, Result<Vec<u8>, String>)],
    n_threads: usize,
    iters: usize,
) -> Vec<String> {
    let workload = Arc::new(workload.to_vec());
    let golden = Arc::new(golden.to_vec());
    let barrier = Arc::new(Barrier::new(n_threads));

    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let workload = workload.clone();
            let golden = golden.clone();
            let barrier = barrier.clone();
            thread::spawn(move || {
                let mut local_failures = Vec::new();
                // Each thread builds its own Nyxstone per arch so we exercise
                // concurrent `build()` calls plus concurrent assemble() across
                // separate instances.
                barrier.wait();
                for iter in 0..iters {
                    for (i, job) in workload.iter().enumerate() {
                        // Build per call — this is what reveals build-time races.
                        let nx = match Nyxstone::new(job.triple, NyxstoneConfig::default()) {
                            Ok(nx) => nx,
                            Err(e) => {
                                local_failures.push(format!(
                                    "t{tid} iter{iter} Nyxstone::new({}): {e}",
                                    job.triple
                                ));
                                continue;
                            }
                        };
                        let actual = nx
                            .assemble(job.asm, job.addr)
                            .map_err(|e| e.to_string());
                        if actual != golden[i].1 {
                            local_failures.push(format!(
                                "t{tid} iter{iter}: {}",
                                fmt_diff(job, &golden[i].1, &actual)
                            ));
                            // Bail this thread on first divergence to make the
                            // log readable when bugs do exist.
                            return local_failures;
                        }
                    }
                }
                local_failures
            })
        })
        .collect();

    handles.into_iter().flat_map(|h| h.join().unwrap()).collect()
}

fn stress_assemble_to_insns(
    workload: &[Job],
    golden: &[(Job, Result<Vec<Instruction>, String>)],
    n_threads: usize,
    iters: usize,
) -> Vec<String> {
    let workload = Arc::new(workload.to_vec());
    let golden = Arc::new(golden.to_vec());
    let barrier = Arc::new(Barrier::new(n_threads));

    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let workload = workload.clone();
            let golden = golden.clone();
            let barrier = barrier.clone();
            thread::spawn(move || {
                let mut local_failures = Vec::new();
                // Per-thread: build one Nyxstone *per arch* (cheaper than
                // per-call) — covers the "share a long-lived instance across
                // many ops" pattern.
                let mut per_arch: std::collections::HashMap<&'static str, Nyxstone> =
                    std::collections::HashMap::new();
                barrier.wait();
                for iter in 0..iters {
                    for (i, job) in workload.iter().enumerate() {
                        let nx = per_arch.entry(job.triple).or_insert_with(|| {
                            Nyxstone::new(job.triple, NyxstoneConfig::default())
                                .unwrap_or_else(|e| {
                                    panic!("Nyxstone::new({}): {e}", job.triple)
                                })
                        });
                        let actual = nx
                            .assemble_to_instructions(job.asm, job.addr)
                            .map_err(|e| e.to_string());
                        if actual != golden[i].1 {
                            local_failures.push(format!(
                                "t{tid} iter{iter}: {}",
                                fmt_diff(job, &golden[i].1, &actual)
                            ));
                            return local_failures;
                        }
                    }
                }
                local_failures
            })
        })
        .collect();

    handles.into_iter().flat_map(|h| h.join().unwrap()).collect()
}

// ---- the tests ---------------------------------------------------------

/// Many threads, each rebuilds Nyxstone *every call*.  Hammers the static
/// mutex in `NyxstoneBuilder::build` + concurrent first-time initialization
/// of LLVM-MC.  This is the gold-standard reproducer for build-race issues.
#[test]
fn stress_rebuild_each_call_assemble() {
    let n_threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8)
        .max(4);
    let iters = 100;

    let workload = workload();
    let golden = golden_assemble(&workload);
    let failures = stress_assemble(&workload, &golden, n_threads, iters);

    if !failures.is_empty() {
        let n_show = failures.len().min(10);
        let total = failures.len();
        panic!(
            "stress_rebuild_each_call_assemble: {total} divergences across {n_threads} \
             threads × {iters} iters × {} jobs/iter:\n{}",
            workload.len(),
            failures[..n_show].join("\n")
        );
    }
}

/// Many threads, each shares one Nyxstone per arch across iterations.  This
/// catches races in `assemble_impl` that wouldn't happen if every call
/// constructed fresh LLVM objects.
#[test]
fn stress_shared_instance_assemble_to_insns() {
    let n_threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8)
        .max(4);
    let iters = 500;

    let workload = workload();
    let golden = golden_assemble_to_insns(&workload);
    let failures = stress_assemble_to_insns(&workload, &golden, n_threads, iters);

    if !failures.is_empty() {
        let n_show = failures.len().min(10);
        let total = failures.len();
        panic!(
            "stress_shared_instance_assemble_to_insns: {total} divergences across \
             {n_threads} threads × {iters} iters × {} jobs/iter:\n{}",
            workload.len(),
            failures[..n_show].join("\n")
        );
    }
}

/// Concurrent assemble + disassemble across threads.  Asymmetric load:
/// catches cross-direction interference.
#[test]
fn stress_mixed_asm_disasm() {
    let n_threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8)
        .max(4);
    let iters = 100;

    // Reference encoding for a couple of fixed inputs we can also disassemble.
    let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default()).unwrap();
    let golden_asm    = nx.assemble("mov rax, rbx", 0x1000).unwrap();
    let golden_disasm = nx.disassemble(&golden_asm, 0x1000, 0).unwrap();
    drop(nx);

    let barrier = Arc::new(Barrier::new(n_threads));
    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let golden_asm    = golden_asm.clone();
            let golden_disasm = golden_disasm.clone();
            let barrier = barrier.clone();
            thread::spawn(move || -> Vec<String> {
                let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default())
                    .expect("Nyxstone::new");
                let mut fails = Vec::new();
                barrier.wait();
                for iter in 0..iters {
                    // Half the threads assemble, half disassemble — same arch,
                    // overlapping in time.
                    if tid % 2 == 0 {
                        let r = nx.assemble("mov rax, rbx", 0x1000)
                            .map_err(|e| e.to_string());
                        if r.as_deref() != Ok(&golden_asm[..]) {
                            fails.push(format!("t{tid} iter{iter} ASM diff: {:?}", r));
                            return fails;
                        }
                    } else {
                        let r = nx.disassemble(&golden_asm, 0x1000, 0)
                            .map_err(|e| e.to_string());
                        if r.as_deref() != Ok(golden_disasm.as_str()) {
                            fails.push(format!("t{tid} iter{iter} DISASM diff: {:?}", r));
                            return fails;
                        }
                    }
                }
                fails
            })
        })
        .collect();

    let mut all_fails: Vec<String> = handles
        .into_iter()
        .flat_map(|h| h.join().unwrap())
        .collect();
    if !all_fails.is_empty() {
        all_fails.truncate(10);
        panic!("stress_mixed_asm_disasm failed:\n{}", all_fails.join("\n"));
    }
}

/// Same-arch, same-input, many threads — minimizes work variance so any
/// observable divergence is purely a race.  Designed to be the most
/// sensitive test in the file.
#[test]
fn stress_hot_loop_single_input() {
    let n_threads = (std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8)).max(8);
    let iters = 10_000;

    let asm  = "mov rax, rbx";
    let addr = 0x1000;
    let golden = {
        let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default()).unwrap();
        nx.assemble(asm, addr).unwrap()
    };

    let barrier = Arc::new(Barrier::new(n_threads));
    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let golden = golden.clone();
            let barrier = barrier.clone();
            thread::spawn(move || -> Result<(), String> {
                let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default())
                    .map_err(|e| format!("new: {e}"))?;
                barrier.wait();
                for iter in 0..iters {
                    let bytes = nx.assemble(asm, addr)
                        .map_err(|e| format!("t{tid} iter{iter} assemble: {e}"))?;
                    if bytes != golden {
                        return Err(format!(
                            "t{tid} iter{iter}: bytes != golden\n  golden={golden:?}\n  bytes={bytes:?}"
                        ));
                    }
                }
                Ok(())
            })
        })
        .collect();

    let errs: Vec<String> = handles
        .into_iter()
        .filter_map(|h| h.join().unwrap().err())
        .collect();
    if !errs.is_empty() {
        panic!("stress_hot_loop_single_input failed:\n{}", errs.join("\n"));
    }
}

/// Construction-only stress: hammer `Nyxstone::new` from many threads with
/// many different triples.  Targets the static `build_common_mutex` and
/// LLVM-MC's first-time-init paths.
#[test]
fn stress_construction_only() {
    let n_threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8)
        .max(8);
    let iters = 100;

    let triples = [
        "x86_64-linux-gnu",
        "i686-linux-gnu",
        "aarch64-linux-gnueabihf",
        "armv7m-none-eabi",
    ];

    let barrier = Arc::new(Barrier::new(n_threads));
    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let barrier = barrier.clone();
            thread::spawn(move || -> Result<(), String> {
                barrier.wait();
                for iter in 0..iters {
                    let triple = triples[(tid + iter) % triples.len()];
                    let _ = Nyxstone::new(triple, NyxstoneConfig::default())
                        .map_err(|e| format!("t{tid} iter{iter} new({triple}): {e}"))?;
                }
                Ok(())
            })
        })
        .collect();

    let errs: Vec<String> = handles
        .into_iter()
        .filter_map(|h| h.join().unwrap().err())
        .collect();
    if !errs.is_empty() {
        panic!("stress_construction_only failed:\n{}", errs.join("\n"));
    }
}

/// Time-bounded flake hunter for AArch64 + ARM Thumb backends — historically
/// the most thread-fragile LLVM-MC paths because they reach into the literal
/// pool and target streamer.  Runs ~30 s of mixed assemble/disassemble
/// across many threads with each input pulled from a varied workload.
/// Counts any deviation from a single-threaded golden run.
#[test]
fn stress_arm_aarch64_long() {
    let n_threads = (std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8))
        .max(8)
        * 2;  // oversubscribe to maximise scheduler-driven interleaving
    let duration = stress_duration();

    // Programs sized to exercise the relax/fixup pipeline (branches, label
    // references, longer streams).
    let arm_program = "\
        .syntax unified\n\
        adr r0, label\n\
        b   label\n\
        mov r1, #42\n\
        label: nop\n\
        bx  lr\n";
    let aarch64_program = "\
        adr x0, label\n\
        b   label\n\
        mov x1, #42\n\
        label: nop\n\
        ret\n";
    let x64_program = "\
        mov rax, qword ptr [rdi + 8]\n\
        add rax, 0x1234\n\
        cmp rax, rbx\n\
        je  done\n\
        ret\n\
        done:\n\
        xor rax, rax\n\
        ret\n";

    let cases: Vec<(&'static str, &'static str)> = vec![
        ("armv7m-none-eabi",       arm_program),
        ("aarch64-linux-gnueabihf", aarch64_program),
        ("x86_64-linux-gnu",       x64_program),
    ];

    // Golden reference: single-threaded encoding of each case.
    let golden: Vec<(&str, &str, Vec<u8>)> = cases
        .iter()
        .map(|(triple, prog)| {
            let nx = Nyxstone::new(triple, NyxstoneConfig::default())
                .unwrap_or_else(|e| panic!("Nyxstone::new({triple}): {e}"));
            let bytes = nx.assemble(prog, 0x1000)
                .unwrap_or_else(|e| panic!("golden assemble({triple}): {e}"));
            (*triple, *prog, bytes)
        })
        .collect();

    let golden = Arc::new(golden);
    let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let counters = Arc::new((0..n_threads).map(|_| std::sync::atomic::AtomicUsize::new(0))
        .collect::<Vec<_>>());

    let start = std::time::Instant::now();
    let barrier = Arc::new(Barrier::new(n_threads));
    let handles: Vec<_> = (0..n_threads)
        .map(|tid| {
            let golden = golden.clone();
            let stop = stop.clone();
            let counters = counters.clone();
            let barrier = barrier.clone();
            thread::spawn(move || -> Result<usize, String> {
                let mut per_arch: std::collections::HashMap<&'static str, Nyxstone> =
                    std::collections::HashMap::new();
                let mut iter: usize = 0;
                barrier.wait();
                loop {
                    if stop.load(std::sync::atomic::Ordering::Relaxed) { break; }
                    for (triple, prog, expected) in golden.iter() {
                        let nx = per_arch.entry(triple).or_insert_with(|| {
                            Nyxstone::new(triple, NyxstoneConfig::default())
                                .unwrap_or_else(|e| panic!("t{tid} Nyxstone::new({triple}): {e}"))
                        });

                        // Assemble.
                        let bytes = match nx.assemble(prog, 0x1000) {
                            Ok(b) => b,
                            Err(e) => return Err(format!(
                                "t{tid} iter{iter} assemble({triple}): {e}"
                            )),
                        };
                        if &bytes != expected {
                            return Err(format!(
                                "t{tid} iter{iter} {triple} byte diff:\n  \
                                 expected={expected:?}\n  actual={bytes:?}"
                            ));
                        }

                        // Disassemble round-trip on the just-assembled bytes.
                        if let Err(e) = nx.disassemble(&bytes, 0x1000, 0) {
                            return Err(format!(
                                "t{tid} iter{iter} disassemble({triple}): {e}"
                            ));
                        }

                        iter += 1;
                        counters[tid].store(iter, std::sync::atomic::Ordering::Relaxed);
                    }
                }
                Ok(iter)
            })
        })
        .collect();

    // Run for `duration`, then signal stop.
    while start.elapsed() < duration {
        thread::sleep(Duration::from_millis(50));
    }
    stop.store(true, std::sync::atomic::Ordering::Relaxed);

    let mut errs = Vec::new();
    let mut total_iters: usize = 0;
    for h in handles {
        match h.join().unwrap() {
            Ok(n) => total_iters += n,
            Err(e) => errs.push(e),
        }
    }
    eprintln!(
        "stress_arm_aarch64_long: {n_threads} threads × {:.1} s = {total_iters} iters, \
         {} errors",
        start.elapsed().as_secs_f32(),
        errs.len()
    );
    if !errs.is_empty() {
        errs.truncate(10);
        panic!("stress_arm_aarch64_long: errors:\n{}", errs.join("\n"));
    }
}

/// Cold-start race hunter.  Spawns many threads with NO prior LLVM
/// initialization, all racing to be the first to call Nyxstone::new() with
/// different architectures.  Designed to expose any window in LLVM-MC's
/// first-time-init or target registry where concurrent calls could see
/// partial state.
#[test]
fn stress_cold_start_construction() {
    let n_threads = (std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8)).max(16) * 2;
    let triples = [
        "x86_64-linux-gnu",
        "i686-linux-gnu",
        "aarch64-linux-gnueabihf",
        "armv7m-none-eabi",
        "armv6m-none-eabi",
        "armv8m.main-none-eabi",
    ];

    // Run many cold-start rounds: each round spawns N threads (each running
    // in a process where LLVM may or may not have been initialized — Cargo's
    // test runner shares one process, so LLVM is init'd once and the windows
    // we care about are subsequent target/MC table accesses).  The point is
    // not to repeat initialization (impossible after the first round) but to
    // multiply scheduling-induced contention windows across rounds.
    let rounds = std::env::var("NYXSTONE_STRESS_SECS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .map(|secs| secs * 7)   // ~7 rounds/s on a modern host
        .unwrap_or(20);
    for round in 0..rounds {
        let barrier = Arc::new(Barrier::new(n_threads));
        let handles: Vec<_> = (0..n_threads).map(|tid| {
            let barrier = barrier.clone();
            let triple = triples[tid % triples.len()];
            thread::spawn(move || -> Result<(), String> {
                barrier.wait();   // all threads enter Nyxstone::new together
                let nx = Nyxstone::new(triple, NyxstoneConfig::default())
                    .map_err(|e| format!("round{round} t{tid} new({triple}): {e}"))?;
                // One assemble + one disassemble to actually exercise post-init.
                let asm = match triple {
                    "x86_64-linux-gnu"           => "mov rax, rbx",
                    "i686-linux-gnu"             => "mov eax, ebx",
                    "aarch64-linux-gnueabihf"    => "mov x0, x1",
                    _                            => "nop",
                };
                let bytes = nx.assemble(asm, 0x1000).map_err(|e| {
                    format!("round{round} t{tid} assemble({triple}): {e}")
                })?;
                nx.disassemble(&bytes, 0x1000, 0).map_err(|e| {
                    format!("round{round} t{tid} disassemble({triple}): {e}")
                })?;
                Ok(())
            })
        }).collect();

        let errs: Vec<String> = handles.into_iter()
            .filter_map(|h| h.join().unwrap().err())
            .collect();
        if !errs.is_empty() {
            panic!("stress_cold_start_construction round {round} failed:\n{}",
                   errs.into_iter().take(10).collect::<Vec<_>>().join("\n"));
        }
    }
}

/// Maximum-pressure stress: 256 threads (heavy oversubscription on most
/// hosts), mixed valid and intentionally-invalid inputs.  Intentional errors
/// exercise the diagnostic-handler path which is the most subtle in
/// nyxstone.cpp.
#[test]
fn stress_max_pressure() {
    let n_threads = 256;
    let duration = stress_duration();

    let valid_cases = [
        ("x86_64-linux-gnu",        "mov rax, rbx",                   0x1000u64),
        ("aarch64-linux-gnueabihf", "mov x0, x1",                     0x2000),
        ("armv7m-none-eabi",        "mov r0, r1",                     0x3000),
    ];
    let invalid_cases = [
        ("x86_64-linux-gnu",        "this_is_not_an_instruction",      0x4000u64),
        ("aarch64-linux-gnueabihf", "garbage instruction",             0x5000),
    ];

    // Golden: collect expected outputs single-threaded.
    let golden_bytes: Vec<Vec<u8>> = valid_cases.iter().map(|(t, asm, addr)| {
        let nx = Nyxstone::new(t, NyxstoneConfig::default())
            .unwrap_or_else(|e| panic!("Nyxstone::new({t}): {e}"));
        nx.assemble(asm, *addr).unwrap_or_else(|e| panic!("golden({t}): {e}"))
    }).collect();

    let golden_bytes = Arc::new(golden_bytes);
    let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let start = std::time::Instant::now();
    let barrier = Arc::new(Barrier::new(n_threads));

    let handles: Vec<_> = (0..n_threads).map(|tid| {
        let golden_bytes = golden_bytes.clone();
        let stop = stop.clone();
        let barrier = barrier.clone();
        thread::spawn(move || -> Result<usize, String> {
            // Each thread holds a per-arch instance.
            let mut per_arch: std::collections::HashMap<&'static str, Nyxstone> =
                std::collections::HashMap::new();
            let mut iter = 0usize;
            barrier.wait();
            loop {
                if stop.load(std::sync::atomic::Ordering::Relaxed) { break; }
                // Round 1: valid inputs, byte-match golden.
                for (i, (triple, asm, addr)) in valid_cases.iter().enumerate() {
                    let nx = per_arch.entry(triple).or_insert_with(|| {
                        Nyxstone::new(triple, NyxstoneConfig::default())
                            .unwrap_or_else(|e| panic!("t{tid} new({triple}): {e}"))
                    });
                    let bytes = nx.assemble(asm, *addr).map_err(|e| {
                        format!("t{tid} iter{iter} VALID assemble({triple}, {asm:?}): {e}")
                    })?;
                    if bytes != golden_bytes[i] {
                        return Err(format!(
                            "t{tid} iter{iter} {triple} {asm:?}: byte diff\n  \
                             expected={:?}\n  actual={bytes:?}",
                            golden_bytes[i]
                        ));
                    }
                }
                // Round 2: invalid inputs — must error, must not crash or
                // succeed spuriously.  Exercises the diagnostic handler path.
                for (triple, asm, addr) in invalid_cases.iter() {
                    let nx = per_arch.entry(triple).or_insert_with(|| {
                        Nyxstone::new(triple, NyxstoneConfig::default())
                            .unwrap_or_else(|e| panic!("t{tid} new({triple}): {e}"))
                    });
                    let r = nx.assemble(asm, *addr);
                    if r.is_ok() {
                        return Err(format!(
                            "t{tid} iter{iter} {triple} {asm:?}: \
                             INVALID input unexpectedly succeeded with bytes={:?}",
                            r.unwrap()
                        ));
                    }
                }
                iter += 1;
            }
            Ok(iter)
        })
    }).collect();

    while start.elapsed() < duration {
        thread::sleep(Duration::from_millis(50));
    }
    stop.store(true, std::sync::atomic::Ordering::Relaxed);

    let mut errs = Vec::new();
    let mut total = 0usize;
    for h in handles {
        match h.join().unwrap() {
            Ok(n) => total += n,
            Err(e) => errs.push(e),
        }
    }
    eprintln!(
        "stress_max_pressure: {n_threads} threads × {:.1}s = {total} valid+invalid rounds, \
         {} errors",
        start.elapsed().as_secs_f32(), errs.len()
    );
    if !errs.is_empty() {
        errs.truncate(10);
        panic!("stress_max_pressure:\n{}", errs.join("\n"));
    }
}

/// Long-form pipeline stress: heavier inputs (multi-line programs with labels),
/// long-lived per-thread instances, mixed assemble/disassemble, many rounds.
/// Reproduces the "rare flake under heavy CI load" pattern.
#[test]
fn stress_long_program_with_labels() {
    let n_threads = (std::thread::available_parallelism().map(|n| n.get()).unwrap_or(8)).max(8) * 2;
    let duration = stress_duration();

    let triple = "x86_64-linux-gnu";
    // ~20 instructions with two label references and a backward branch.
    let program = "\
        start:\n\
        push rbp\n\
        mov  rbp, rsp\n\
        sub  rsp, 16\n\
        mov  rax, 1\n\
        mov  rbx, 2\n\
        loop_top:\n\
        add  rax, rbx\n\
        cmp  rax, 100\n\
        jl   loop_top\n\
        mov  rcx, [rbp - 8]\n\
        test rcx, rcx\n\
        jne  exit\n\
        call do_thing\n\
        exit:\n\
        leave\n\
        ret\n\
        do_thing:\n\
        xor  rax, rax\n\
        ret\n";

    let golden = {
        let nx = Nyxstone::new(triple, NyxstoneConfig::default()).unwrap();
        nx.assemble(program, 0x1000).unwrap()
    };

    let golden = Arc::new(golden);
    let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
    let start = std::time::Instant::now();
    let barrier = Arc::new(Barrier::new(n_threads));

    let handles: Vec<_> = (0..n_threads).map(|tid| {
        let golden = golden.clone();
        let stop = stop.clone();
        let barrier = barrier.clone();
        thread::spawn(move || -> Result<usize, String> {
            let nx = Nyxstone::new(triple, NyxstoneConfig::default())
                .map_err(|e| format!("t{tid} new: {e}"))?;
            let mut iter = 0usize;
            barrier.wait();
            loop {
                if stop.load(std::sync::atomic::Ordering::Relaxed) { break; }
                let bytes = nx.assemble(program, 0x1000).map_err(|e| {
                    format!("t{tid} iter{iter} assemble: {e}")
                })?;
                if bytes.as_slice() != golden.as_slice() {
                    return Err(format!(
                        "t{tid} iter{iter} byte diff: expected {} bytes, got {} bytes",
                        golden.len(), bytes.len()
                    ));
                }
                let _insns = nx.disassemble_to_instructions(&bytes, 0x1000, 0)
                    .map_err(|e| format!("t{tid} iter{iter} disasm: {e}"))?;
                iter += 1;
            }
            Ok(iter)
        })
    }).collect();

    while start.elapsed() < duration {
        thread::sleep(Duration::from_millis(50));
    }
    stop.store(true, std::sync::atomic::Ordering::Relaxed);

    let mut errs = Vec::new();
    let mut total = 0usize;
    for h in handles {
        match h.join().unwrap() {
            Ok(n) => total += n,
            Err(e) => errs.push(e),
        }
    }
    eprintln!(
        "stress_long_program_with_labels: {n_threads} threads × {:.1}s = {total} iters, \
         {} errors",
        start.elapsed().as_secs_f32(), errs.len()
    );
    if !errs.is_empty() {
        errs.truncate(10);
        panic!("stress_long_program_with_labels:\n{}", errs.join("\n"));
    }
}

/// Repeated runs of stress_hot_loop_single_input — flake hunter.  Enabled
/// only when NYXSTONE_FLAKE_HUNT=1 because it takes >30 s and we don't want
/// CI minutes burned on every PR.
#[test]
fn stress_hot_loop_repeated() {
    if std::env::var("NYXSTONE_FLAKE_HUNT").as_deref() != Ok("1") {
        eprintln!("(skipped — set NYXSTONE_FLAKE_HUNT=1 to enable)");
        return;
    }

    let n_threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(8)
        .max(8);
    let outer = 100;
    let inner_iters = 100;

    let asm  = "mov rax, rbx";
    let addr = 0x1000;
    let golden = {
        let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default()).unwrap();
        nx.assemble(asm, addr).unwrap()
    };

    for round in 0..outer {
        let barrier = Arc::new(Barrier::new(n_threads));
        let handles: Vec<_> = (0..n_threads)
            .map(|tid| {
                let golden = golden.clone();
                let barrier = barrier.clone();
                thread::spawn(move || -> Result<(), String> {
                    let nx = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default())
                        .map_err(|e| format!("new: {e}"))?;
                    barrier.wait();
                    for iter in 0..inner_iters {
                        let bytes = nx.assemble(asm, addr)
                            .map_err(|e| format!("round{round} t{tid} iter{iter} \
                                                  assemble: {e}"))?;
                        if bytes != golden {
                            return Err(format!(
                                "round{round} t{tid} iter{iter}: bytes != golden"
                            ));
                        }
                    }
                    Ok(())
                })
            })
            .collect();
        let errs: Vec<String> = handles
            .into_iter()
            .filter_map(|h| h.join().unwrap().err())
            .collect();
        if !errs.is_empty() {
            panic!("stress_hot_loop_repeated round {round} failed:\n{}",
                   errs.join("\n"));
        }
        // Tiny pause between rounds — lets the kernel re-balance and slightly
        // varies the contention timing.
        thread::sleep(Duration::from_millis(2));
    }
}
