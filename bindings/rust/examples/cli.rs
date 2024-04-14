extern crate anyhow;
extern crate clap;
extern crate nyxstone;

use std::{collections::HashMap, ops::Deref, str::FromStr};

use anyhow::{anyhow, ensure, Context, Result};
use clap::{Parser, Subcommand};

use nyxstone::{Instruction, IntegerBase, Nyxstone, NyxstoneConfig};

#[derive(Clone, Subcommand)]
enum Command {
    Assemble {
        #[arg(short, long, value_parser = parse_label)]
        labels: Option<Vec<(String, u64)>>,
        assembly: String,
    },
    Disassemble {
        #[arg(value_parser = clap::value_parser!(Disassembly))]
        disassembly: Disassembly,
    },
}

#[derive(Parser)]
struct Args {
    #[arg(short = 'a', long)]
    architecture: String,

    #[arg(short = 's', long, value_parser = parse_maybe_hex, default_value_t = 0)]
    address: u64,

    #[command(subcommand)]
    command: Command,
}

fn print_instructions(instructions: &[Instruction]) {
    for instruction in instructions {
        println!(
            "{:08x}: {:16} -- {:02x?}",
            instruction.address, instruction.assembly, instruction.bytes
        );
    }
}

fn main() -> Result<()> {
    let cli = Args::parse();

    let nyxstone = Nyxstone::new(
        &cli.architecture,
        NyxstoneConfig {
            immediate_style: IntegerBase::HexPrefix,
            ..Default::default()
        },
    )?;

    let address = cli.address;

    match cli.command {
        Command::Assemble { labels, assembly } => {
            let labels = labels.unwrap_or(vec![]);

            let labels = labels
                .iter()
                .map(|(s, a)| (s.as_str(), *a))
                .collect::<HashMap<&str, u64>>();

            let instructions = nyxstone.assemble_to_instructions(&assembly, address, &labels)?;

            print_instructions(&instructions);
        }
        Command::Disassemble { disassembly } => {
            let disassembly = nyxstone.disassemble_to_instructions(&disassembly, address, 0)?;

            print_instructions(&disassembly);
        }
    }

    Ok(())
}

#[derive(Clone)]
struct Disassembly(Vec<u8>);

impl FromStr for Disassembly {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self> {
        ensure!(s.is_ascii(), "Can only parse disassembly from an ascii string");

        let cleaned: String = s
            .trim_start_matches('[')
            .trim_end_matches(']')
            .split_ascii_whitespace()
            .map(|s| s.trim_end_matches(','))
            .collect();

        ensure!(
            cleaned.len() % 2 == 0,
            "Invalid number of hexadecimal characters in disassembly"
        );

        let bytes = cleaned
            .as_bytes()
            .chunks(2)
            .map(std::str::from_utf8)
            .map(|v| v.map(|h| u8::from_str_radix(h, 16).with_context(|| anyhow!("Could not parse hex byte: {h}"))))
            .flatten()
            .collect::<Result<_>>()?;

        Ok(Self(bytes))
    }
}

impl Deref for Disassembly {
    type Target = Vec<u8>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

fn parse_maybe_hex(s: &str) -> Result<u64, std::num::ParseIntError> {
    if let Some(hex_addr) = s.strip_prefix("0x") {
        u64::from_str_radix(hex_addr, 16)
    } else {
        u64::from_str(s)
    }
}

fn parse_label(s: &str) -> Result<(String, u64)> {
    let pos = s
        .find('=')
        .ok_or_else(|| anyhow!("invalid KEY=value: no `=` found in `{s}`"))?;

    let addr = &s[pos + 1..];

    let addr = parse_maybe_hex(addr)?;

    Ok((s[..pos].parse()?, addr))
}
