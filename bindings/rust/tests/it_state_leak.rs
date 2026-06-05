//! Hypothesis: the ARM Thumb disassembler carries IT-block (ITSTATE) state as
//! mutable member state across `getInstruction` calls. Nyxstone caches and
//! REUSES one `MCDisassembler` per instance across every `disassemble` call
//! (src/nyxstone.cpp: `disasm_context` / `disassembler`). So if one
//! `disassemble` call ends while an IT block is still "open" (the predicated
//! slots were never all consumed), the leftover ITSTATE could leak into the
//! NEXT `disassemble` call on the same instance and mis-decode unrelated bytes.
//!
//! This is:
//!   - Thumb-specific (IT blocks are Thumb only)
//!   - sequence-dependent (depends on what the *previous* call disassembled)
//!   - input-dependent and therefore looks "non-deterministic" / load-correlated
//!
//! which matches every symptom in THREADING_INVESTIGATION.md.

use nyxstone::{Nyxstone, NyxstoneConfig};

const TRIPLE: &str = "armv7m-none-eabi";

fn nx() -> Nyxstone {
    Nyxstone::new(TRIPLE, NyxstoneConfig::default()).expect("build nyxstone")
}

/// Disassemble `mov r0, r1` on a *fresh* instance to get the canonical answer.
#[test]
fn baseline_mov_on_fresh_instance() {
    let asm = nx();
    let bytes = asm.assemble("mov r0, r1", 0x1000).expect("assemble mov");
    eprintln!("mov r0, r1 bytes = {:02x?}", bytes);
    let disas = nx().disassemble(&bytes, 0x1000, 0).expect("disassemble mov");
    eprintln!("fresh decode: {:?}", disas.trim());
    assert!(
        disas.contains("mov") && !disas.contains("eq") && !disas.contains("ne"),
        "fresh-instance decode of `mov r0,r1` should be unpredicated, got {:?}",
        disas
    );
}

/// The core test: reuse ONE instance. First disassemble a buffer that ends
/// right after an `IT` instruction (block opened, predicated slots unconsumed),
/// then disassemble a *separate* plain `mov r0, r1`. If ITSTATE leaks, the mov
/// gets a stale predicate (decoded as `moveq`/`movne`/etc.) or fails to decode.
#[test]
fn it_state_leaks_across_disassemble_calls() {
    let asm = nx();

    // Bytes for an IT-block opener. `itt eq` sets up two predicated slots.
    let it_bytes = asm.assemble("itt eq", 0x2000).expect("assemble itt eq");
    eprintln!("itt eq bytes = {:02x?}", it_bytes);

    let mov_bytes = asm.assemble("mov r0, r1", 0x3000).expect("assemble mov");
    eprintln!("mov r0, r1 bytes = {:02x?}", mov_bytes);

    // ONE reused instance.
    let reused = nx();

    // Call #1: disassemble ONLY the IT opener. The buffer ends before the
    // predicated instructions, so the block is left "open".
    let first = reused.disassemble(&it_bytes, 0x2000, 0);
    eprintln!("call#1 (itt eq) -> {:?}", first.as_ref().map(|s| s.trim()));

    // Call #2: disassemble an unrelated plain `mov r0, r1` on the SAME instance.
    let second = reused.disassemble(&mov_bytes, 0x3000, 0);
    eprintln!("call#2 (mov r0,r1, reused instance) -> {:?}", second.as_ref().map(|s| s.trim()));

    // Ground truth from a fresh instance.
    let clean = nx().disassemble(&mov_bytes, 0x3000, 0).expect("fresh decode");
    eprintln!("clean (mov r0,r1, fresh instance)   -> {:?}", clean.trim());

    match second {
        Err(e) => panic!(
            "IT STATE LEAK CONFIRMED: reused-instance decode of `mov r0,r1` FAILED \
             after a prior IT-opener, but a fresh instance decodes it fine ({:?}). Error: {}",
            clean.trim(),
            e
        ),
        Ok(s) => {
            assert_eq!(
                s.trim(),
                clean.trim(),
                "IT STATE LEAK CONFIRMED: reused-instance decode {:?} differs from \
                 fresh-instance decode {:?} for the same bytes {:02x?}",
                s.trim(),
                clean.trim(),
                mov_bytes
            );
        }
    }
}

/// Boundary check: a COMPLETE IT block (opener + all predicated slots) should
/// leave the disassembler in a clean state, so a following decode on the same
/// instance is correct. This proves the trigger is specifically "buffer ends
/// with an UNFINISHED IT block", not "any IT block was ever decoded".
#[test]
fn complete_it_block_leaves_clean_state() {
    let asm = nx();
    let block = asm
        .assemble("itt eq\n addeq r0, r1\n addeq r2, r3", 0x6000)
        .expect("assemble full it block");
    let mov_bytes = asm.assemble("mov r0, r1", 0x7000).expect("assemble mov");

    let reused = nx();
    let first = reused.disassemble(&block, 0x6000, 0).expect("disassemble full block");
    eprintln!("call#1 (full itt block) -> {:?}", first.trim());
    let second = reused.disassemble(&mov_bytes, 0x7000, 0).expect("decode mov after full block");
    eprintln!("call#2 (mov after full block) -> {:?}", second.trim());

    assert_eq!(
        second.trim(),
        "mov r0, r1",
        "a COMPLETE IT block should leave clean state; got {:?}",
        second.trim()
    );
}

/// Same idea but with `count`-limited disassembly: assemble a full IT block,
/// then disassemble with count=1 so we stop right after the IT opener, leaving
/// the block open. Then reuse the instance for unrelated bytes.
#[test]
fn it_state_leaks_via_count_limit() {
    let asm = nx();

    let block = asm
        .assemble("itt eq\n addeq r0, r1\n addeq r2, r3", 0x4000)
        .expect("assemble it block");
    eprintln!("itt block bytes = {:02x?}", block);

    let mov_bytes = asm.assemble("adds r4, r5, r6", 0x5000).expect("assemble adds");
    eprintln!("adds bytes = {:02x?}", mov_bytes);

    let reused = nx();

    // Stop after just the IT opener (count = 1).
    let first = reused.disassemble(&block, 0x4000, 1);
    eprintln!("call#1 (count=1 of it block) -> {:?}", first.as_ref().map(|s| s.trim()));

    let second = reused.disassemble(&mov_bytes, 0x5000, 0);
    eprintln!("call#2 (adds, reused) -> {:?}", second.as_ref().map(|s| s.trim()));

    let clean = nx().disassemble(&mov_bytes, 0x5000, 0).expect("fresh decode");
    eprintln!("clean (adds, fresh)   -> {:?}", clean.trim());

    match second {
        Err(e) => panic!(
            "IT STATE LEAK (count) CONFIRMED: reused decode FAILED; fresh = {:?}; err = {}",
            clean.trim(),
            e
        ),
        Ok(s) => assert_eq!(
            s.trim(),
            clean.trim(),
            "IT STATE LEAK (count) CONFIRMED: reused {:?} != fresh {:?}",
            s.trim(),
            clean.trim()
        ),
    }
}
