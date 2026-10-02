use std::collections::HashMap;

use anyhow::Result;
use nyxstone::{Nyxstone, NyxstoneConfig, Relocation};

const R_386_PC32: u32 = 2;
const R_X86_64_PLT32: u32 = 4;
const R_ARM_THM_CALL: u32 = 10;
const R_AARCH64_ADR_PREL_PG_HI21: u32 = 275;
const R_AARCH64_ADD_ABS_LO12_NC: u32 = 277;
const R_RISCV_CALL: u32 = 18;
const R_RISCV_CALL_PLT: u32 = 19;

/// Gives an empty label map, for an assembly that refers only to external symbols.
fn no_labels() -> HashMap<&'static str, u64> {
    HashMap::new()
}

/// Builds the expected relocation. `addend` is `None` for a REL target, which keeps the addend in the field.
fn relocation(address: u64, kind: u32, symbol: &str, addend: Option<i64>) -> Relocation {
    Relocation {
        address,
        kind,
        symbol: symbol.into(),
        has_addend: addend.is_some(),
        addend: addend.unwrap_or(0),
    }
}

#[test]
fn x86_32_call_to_an_extern_keeps_its_implicit_addend() -> Result<()> {
    let nyxstone = Nyxstone::new("i686-linux-gnu", NyxstoneConfig::default())?;

    let (instructions, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("call ext", 0x1000, &no_labels(), &["ext"])?;

    // ELF i386 uses REL: the field holds the addend, which is -4 for a call.
    assert_eq!(instructions.len(), 1);
    assert_eq!(instructions[0].bytes, vec![0xe8, 0xfc, 0xff, 0xff, 0xff]);
    assert_eq!(relocations, vec![relocation(0x1001, R_386_PC32, "ext", None)]);
    Ok(())
}

#[test]
fn x86_32_relocates_only_the_extern_next_to_a_label() -> Result<()> {
    let nyxstone = Nyxstone::new("i686-linux-gnu", NyxstoneConfig::default())?;
    let labels = HashMap::from([(".lbl", 0x1100_u64)]);

    let (instructions, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("jmp .lbl\ncall ext", 0x1000, &labels, &["ext"])?;

    // The label is in the same section, so the jump is resolved: 0x1005 + 0xfb = 0x1100.
    assert_eq!(instructions[0].bytes, vec![0xe9, 0xfb, 0x00, 0x00, 0x00]);
    assert_eq!(relocations, vec![relocation(0x1006, R_386_PC32, "ext", None)]);
    Ok(())
}

#[test]
fn relocations_are_sorted_by_address() -> Result<()> {
    let nyxstone = Nyxstone::new("i686-linux-gnu", NyxstoneConfig::default())?;

    let (_, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("call b\ncall a", 0x1000, &no_labels(), &["a", "b"])?;

    assert_eq!(
        relocations,
        vec![
            relocation(0x1001, R_386_PC32, "b", None),
            relocation(0x1006, R_386_PC32, "a", None),
        ]
    );
    Ok(())
}

#[test]
fn an_undefined_name_that_is_not_extern_is_still_an_error() -> Result<()> {
    let nyxstone = Nyxstone::new("i686-linux-gnu", NyxstoneConfig::default())?;

    let result = nyxstone.assemble_to_instructions_with_relocations("call typo", 0x1000, &no_labels(), &["ext"]);

    assert!(result.is_err_and(|error| error.to_string().contains("Label undefined")));
    Ok(())
}

#[test]
fn a_label_used_as_an_absolute_value_keeps_the_bytes_of_assemble_with() -> Result<()> {
    let nyxstone = Nyxstone::new("i686-linux-gnu", NyxstoneConfig::default())?;
    let labels = HashMap::from([(".lbl", 0x2000_u64)]);

    // LLVM keeps a relocation against `.text` for this reference. It is not for an extern, so it is not given,
    // and the bytes are the same as without relocations.
    let (instructions, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("mov eax, offset .lbl", 0x1000, &labels, &["ext"])?;

    assert_eq!(relocations, vec![]);
    assert_eq!(
        instructions[0].bytes,
        nyxstone.assemble_with("mov eax, offset .lbl", 0x1000, &labels)?
    );
    Ok(())
}

#[test]
fn x86_64_call_to_an_extern_has_an_explicit_addend() -> Result<()> {
    let nyxstone = Nyxstone::new("x86_64-linux-gnu", NyxstoneConfig::default())?;

    let (_, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("call ext", 0x1000, &no_labels(), &["ext"])?;

    assert_eq!(relocations, vec![relocation(0x1001, R_X86_64_PLT32, "ext", Some(-4))]);
    Ok(())
}

#[test]
fn thumb_branch_to_an_extern_skips_the_label_checks_at_a_halfword_address() -> Result<()> {
    let nyxstone = Nyxstone::new("armv8m.main-none-eabi", NyxstoneConfig::default())?;

    // An address that is 2 but not 4 byte aligned makes Nyxstone prepend a bkpt, which must not move the
    // relocation.
    let (instructions, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("bl ext", 0x1002, &no_labels(), &["ext"])?;

    assert_eq!(instructions.len(), 1);
    assert_eq!(instructions[0].address, 0x1002);
    assert_eq!(relocations, vec![relocation(0x1002, R_ARM_THM_CALL, "ext", None)]);
    Ok(())
}

#[test]
fn aarch64_page_reference_to_an_extern_is_left_to_the_linker() -> Result<()> {
    let nyxstone = Nyxstone::new("aarch64-linux-gnueabihf", NyxstoneConfig::default())?;

    let (instructions, relocations) = nyxstone.assemble_to_instructions_with_relocations(
        "adrp x0, ext\nadd x0, x0, :lo12:ext",
        0x1000,
        &no_labels(),
        &["ext"],
    )?;

    // Nyxstone resolves `adrp` against the runtime address for a label, but not for an extern.
    assert_eq!(instructions[0].bytes, vec![0x00, 0x00, 0x00, 0x90]);
    assert_eq!(
        relocations,
        vec![
            relocation(0x1000, R_AARCH64_ADR_PREL_PG_HI21, "ext", Some(0)),
            relocation(0x1004, R_AARCH64_ADD_ABS_LO12_NC, "ext", Some(0)),
        ]
    );
    Ok(())
}

#[test]
fn riscv_call_to_an_extern_has_one_relocation_for_both_instructions() -> Result<()> {
    let nyxstone = Nyxstone::new("riscv64-linux-gnu", NyxstoneConfig::default())?;

    let (instructions, relocations) =
        nyxstone.assemble_to_instructions_with_relocations("call ext", 0x1000, &no_labels(), &["ext"])?;

    // One relocation covers the `auipc` and the `jalr` of the pseudo instruction. LLVM 15 gives `R_RISCV_CALL`,
    // later versions give `R_RISCV_CALL_PLT`.
    assert_eq!(instructions[0].bytes.len(), 8);
    let kind = relocations
        .first()
        .map(|relocation| relocation.kind)
        .unwrap_or_default();
    assert!(
        [R_RISCV_CALL, R_RISCV_CALL_PLT].contains(&kind),
        "unexpected relocation type {kind}"
    );
    assert_eq!(relocations, vec![relocation(0x1000, kind, "ext", Some(0))]);
    Ok(())
}

#[test]
fn riscv_pcrel_pair_for_an_extern_is_an_error() -> Result<()> {
    let nyxstone = Nyxstone::new("riscv64-linux-gnu", NyxstoneConfig::default())?;

    // The `%pcrel_lo` relocation names the label of the `auipc`, not `ext`, so it cannot be given. Without it the
    // linker would leave the low 12 bits at 0.
    for assembly in [
        "lla a0, ext",
        "la a0, ext",
        "1: auipc a0, %pcrel_hi(ext)\naddi a0, a0, %pcrel_lo(1b)",
    ] {
        let result = nyxstone.assemble_to_instructions_with_relocations(assembly, 0x1000, &no_labels(), &["ext"]);

        assert!(
            result.is_err_and(|error| error
                .to_string()
                .contains("pairs with the %pcrel_hi of the external symbol 'ext'")),
            "'{assembly}' must be an error"
        );
    }
    Ok(())
}
