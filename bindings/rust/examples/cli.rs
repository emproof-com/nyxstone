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
        /// Custom label address pairs, formatted like "label=<address>". Multiple labels should be specified by using
        /// this flag multiple times, i.e. -l "label0=<address0>" -l "label1=<address1>"
        #[arg(short, long = "label", value_parser = parse_label)]
        labels: Option<Vec<(String, u64)>>,

        assembly: String,
    },
    Disassemble {
        #[arg(value_parser = clap::value_parser!(Disassembly))]
        disassembly: Disassembly,
    },
}

/// Rust CLI for Nyxstone
#[derive(Parser)]
struct Args {
    /// LLVM triple or architecture identifier of triple. For the most common architectures, we recommend:
    ///
    /// - x86_32: `i686-linux-gnu`
    ///
    /// - x86_64: `x86_64-linux-gnu`
    ///
    /// - armv6m: `armv6m-none-eabi`
    ///
    /// - armv7m: `armv7m-none-eabi`
    ///
    /// - armv8m: `armv8m.main-none-eabi`
    ///
    /// - aarch64: `aarch64-linux-gnueabihf`
    ///
    /// Using shorthand identifiers like `arm` can lead to Nyxstone not being able to assemble certain instructions.
    #[arg(short = 'a', long)]
    architecture: String,

    /// Start address of assembly/disassembly
    #[arg(short = 's', long, value_parser = parse_maybe_hex, default_value_t = 0)]
    address: u64,

    /// LLVM cpu specifier, refer to `llc -march=ARCH -mcpu=help` for a comprehensive list
    #[arg(short = 'c', long)]
    cpu: Option<String>,

    /// LLVM features to enable/disable, comma seperated feature strings prepended by '+' or '-' to enable or disable
    /// respectively. Refer to `llc -march=ARCH -mattr=help` for a comprehensive list
    #[arg(short = 'f', long)]
    features: Option<String>,

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
            cpu: &cli.cpu.unwrap_or_default(),
            features: &cli.features.unwrap_or_default(),
            immediate_style: IntegerBase::HexPrefix,
        },
    )?;

    let address = cli.address;

    match cli.command {
        Command::Assemble { labels, assembly } => {
            let labels: HashMap<String, u64> = labels.map(|labels| labels.into_iter().collect()).unwrap_or_default();

            let instructions = nyxstone.assemble_to_instructions_with(&assembly, address, &labels)?;

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
