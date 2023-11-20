extern crate anyhow;
extern crate nyxstone;

use std::collections::HashMap;

use anyhow::Result;
use nyxstone::{IntegerBase, Nyxstone, NyxstoneConfig};

fn main() -> Result<()> {
    // Creating a nyxstone instance can fail, for example if the triple is invalid.
    let nyxstone = Nyxstone::new("x86_64", NyxstoneConfig::default())?;

    let instructions = nyxstone.assemble_to_instructions(
        "mov rax, rbx; cmp rax, rdx; jne .label",
        0x100,
        &HashMap::from([(".label", 0x1200)]),
    )?;

    println!("Assembled: ");
    for instr in instructions {
        println!("0x{:04x}: {:15} - {:02x?}", instr.address, instr.assembly, instr.bytes);
    }

    let disassembly = nyxstone.disassemble(
        &[0x31, 0xd8],
        /* address= */ 0x0,
        /* #instructions= (0 = all)*/ 0,
    )?;

    assert_eq!(disassembly, "xor eax, ebx\n".to_owned());

    let config = NyxstoneConfig {
        immediate_style: IntegerBase::HexPrefix,
        ..Default::default()
    };
    let nyxstone = Nyxstone::new("x86_64", config)?;

    assert_eq!(
        nyxstone.disassemble(&[0x83, 0xc0, 0x01], 0, 0)?,
        "add eax, 0x1\n".to_owned()
    );

    Ok(())
}
