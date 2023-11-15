use anyhow::anyhow;
use ffi::create_nyxstone_ffi;
use ffi::LabelDefinition;

// Re-export Instruction
pub use crate::ffi::Instruction;

/// Public interface for calling nyxstone from rust.
/// # Examples
///
/// ```rust
/// # use std::collections::HashMap;
/// # use nyxstone::{Nyxstone, NyxstoneConfig, Instruction};
/// # fn main() -> anyhow::Result<()> {
/// let nyxstone = Nyxstone::new("x86_64", NyxstoneConfig::default())?;
///
/// let instructions = nyxstone.assemble_to_instructions("mov rax, rbx", 0x1000, &HashMap::new())?;
///
/// assert_eq!(
///      instructions,
///      vec![Instruction {
///          address: 0x1000,
///          assembly: "mov rax, rbx".into(),
///          bytes: vec![0x48, 0x89, 0xd8]
///      }]
/// );
/// # Ok(())
/// # }
/// ```
pub struct Nyxstone {
    /// The c++ `unique_ptr` holding the actual `NyxstoneFFI` instance.
    inner: cxx::UniquePtr<ffi::NyxstoneFFI>,
}

/// Configuration options for the integer style of immediates in disassembly output.
#[derive(Debug, PartialEq, Eq, Default, Clone, Copy)]
pub enum IntegerBase {
    /// Immediates are represented in decimal format.
    #[default]
    Dec = 0,
    /// Immediates are represented in hex format, prepended with 0x, for example: 0xff.
    HexPrefix = 1,
    /// Immediates are represented in hex format, suffixed with h, for example: 0ffh.
    HexSuffix = 2,
}

impl From<IntegerBase> for ffi::IntegerBase {
    fn from(val: IntegerBase) -> Self {
        match val {
            IntegerBase::Dec => ffi::IntegerBase::Dec,
            IntegerBase::HexPrefix => ffi::IntegerBase::HexPrefix,
            IntegerBase::HexSuffix => ffi::IntegerBase::HexSuffix,
        }
    }
}

impl<'name> LabelDefinition<'name> {
    fn new(name: &'name str, address: u64) -> Self {
        LabelDefinition { name, address }
    }
}

impl<'name> From<(&&'name str, &u64)> for LabelDefinition<'name> {
    fn from(value: (&&'name str, &u64)) -> Self {
        LabelDefinition::new(value.0, *value.1)
    }
}

impl Nyxstone {
    /// Builds a Nyxstone instance with specific configuration.
    ///
    /// # Parameters:
    /// - `target_triple`: Llvm target triple or architecture identifier of triple.
    /// - `config`: Optional configuration for the `Nyxstone` instance.
    ///
    /// # Returns
    /// Ok() and the Nyxstone instance on success, Err() otherwise.
    ///
    /// # Errors
    /// Errors occur when the LLVM triple was not supplied to the builder or LLVM fails.
    pub fn new(target_triple: &str, config: NyxstoneConfig) -> anyhow::Result<Nyxstone> {
        let result = create_nyxstone_ffi(
            target_triple,
            config.cpu,
            config.features,
            config.immediate_style.into(),
        );

        if !result.error.is_empty() {
            return Err(anyhow!("{}", result.error));
        }

        Ok(Nyxstone { inner: result.ok })
    }

    /// Translates assembly instructions at a given start address to bytes.
    ///
    /// # Note:
    /// Does not support assembly directives that impact the layout (f. i., .section, .org).
    ///
    /// # Parameters:
    /// - `assembly`: The instructions to assemble.
    /// - `address`: The start location of the instructions.
    /// - `labels`: Additional label definitions by absolute address, expects a reference to some `Map<&str, u64>` which can be iterated over.
    ///
    /// # Returns:
    /// Ok() and bytecode on success, Err() otherwise.
    pub fn assemble_to_bytes<'iter, 'label: 'iter, T>(
        &self,
        assembly: &str,
        address: u64,
        labels: &'iter T,
    ) -> anyhow::Result<Vec<u8>>
    where
        &'iter T: IntoIterator<Item = (&'iter &'label str, &'iter u64)>,
    {
        let labels: Vec<LabelDefinition> = labels.into_iter().map(LabelDefinition::from).collect();

        let byte_result = self.inner.assemble_to_bytes(assembly, address, &labels);

        if !byte_result.error.is_empty() {
            return Err(anyhow!(
                "Error during assemble (= '{assembly}' at {address}): {}.",
                byte_result.error
            ));
        }

        Ok(byte_result.ok)
    }

    /// Translates assembly instructions at a given start address to instruction details containing bytes.
    ///
    /// # Note:
    /// Does not support assembly directives that impact the layout (f. i., .section, .org).
    ///
    /// # Parameters:
    /// - `assembly`: The instructions to assemble.
    /// - `address`: The start location of the instructions.
    /// - `labels`: Additional label definitions by absolute address, expects a reference to some `Map<&str, u64>` which can be iterated over.
    ///
    /// # Returns:
    /// Ok() and instruction details on success, Err() otherwise.
    pub fn assemble_to_instructions<'iter, 'label: 'iter, T>(
        &self,
        assembly: &str,
        address: u64,
        labels: &'iter T,
    ) -> anyhow::Result<Vec<Instruction>>
    where
        &'iter T: IntoIterator<Item = (&'iter &'label str, &'iter u64)>,
    {
        let labels: Vec<LabelDefinition> = labels.into_iter().map(LabelDefinition::from).collect();

        let instr_result = self.inner.assemble_to_instructions(assembly, address, &labels);

        if !instr_result.error.is_empty() {
            return Err(anyhow!("Error during disassembly: {}.", instr_result.error));
        }

        Ok(instr_result.ok)
    }

    /// Translates bytes to disassembly text at a given start address.
    ///
    /// # Parameters:
    /// - `bytes`: The bytes to be disassembled.
    /// - `address`: The start address of the bytes.
    /// - `count`: Number of instructions to be disassembled. If zero is supplied, all instructions are disassembled.
    ///
    /// # Returns:
    /// Ok() and disassembly text on success, Err() otherwise.
    pub fn disassemble_to_text(&self, bytes: &[u8], address: u64, count: usize) -> anyhow::Result<String> {
        let text_result = self.inner.disassemble_to_text(bytes, address, count);

        if !text_result.error.is_empty() {
            return Err(anyhow!("Error during disassembly: {}.", text_result.error));
        }

        Ok(text_result.ok)
    }

    /// Translates bytes to instruction details containing disassembly text at a given start address.
    ///
    /// # Parameters:
    /// - `bytes`: The bytes to be disassembled.
    /// - `address`: The start address of the bytes.
    /// - `count`: Number of instructions to be disassembled. If zero is supplied, all instructions are disassembled.
    ///
    /// # Returns:
    /// Ok() and Instruction details on success, Err() otherwise.
    pub fn disassemble_to_instructions(
        &self,
        bytes: &[u8],
        address: u64,
        count: usize,
    ) -> anyhow::Result<Vec<Instruction>> {
        let instr_result = self.inner.disassemble_to_instructions(bytes, address, count);

        if !instr_result.error.is_empty() {
            return Err(anyhow!("Error during disassembly: {}.", instr_result.error));
        }

        Ok(instr_result.ok)
    }
}

unsafe impl Send for Nyxstone {}

/// Initialization configuration for Nyxstone
#[derive(Debug, Default)]
pub struct NyxstoneConfig<'a, 'b> {
    /// The LLVM cpu identifier, empty for no specific cpu target.
    pub cpu: &'a str,
    /// An LLVM feature string, features are comma seperated strings, which are prepended with '+' when enabled and '-' if disabled.
    pub features: &'b str,
    /// The printing style of immediates.
    pub immediate_style: IntegerBase,
}

#[cxx::bridge]
mod ffi {
    /// Defines the location of a label by absolute address.
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub struct LabelDefinition<'name> {
        /// Name of the label.
        pub name: &'name str,
        /// Absolute address of the label.
        pub address: u64,
    }

    /// Instruction details
    #[derive(Clone, Debug, PartialEq, Eq)]
    pub struct Instruction {
        /// Absolute address of the instruction.
        address: u64,
        /// Assembly string representing the instruction.
        assembly: String,
        /// Byte code of the instruction.
        bytes: Vec<u8>,
    }

    pub struct ByteResult {
        pub ok: Vec<u8>,
        pub error: String,
    }

    pub struct InstructionResult {
        pub ok: Vec<Instruction>,
        pub error: String,
    }

    pub struct StringResult {
        pub ok: String,
        pub error: String,
    }

    pub struct NyxstoneResult {
        pub ok: UniquePtr<NyxstoneFFI>,
        pub error: String,
    }

    /// Configuration options for the integer style of immediates in disassembly output.
    pub enum IntegerBase {
        /// Immediates are represented in decimal format.
        Dec = 0,
        /// Immediates are represented in hex format, prepended with 0x, for example: 0xff.
        HexPrefix = 1,
        /// Immediates are represented in hex format, suffixed with h, for example: 0ffh.
        HexSuffix = 2,
    }

    unsafe extern "C++" {
        include!("nyxstone/src/nyxstone_ffi.hpp");

        type NyxstoneFFI;

        /// Constructs a Nyxstone instance for the architecture and cpu specified by the llvm-style target triple and
        /// cpu. Also allows enabling and disabling features via the `features` string.
        /// Features are comma-seperated feature strings, which start with a plus if they should be enabled and a minus
        /// if they should be disabled.
        /// Params:
        /// - triple_name: The llvm triple.
        /// - cpu: The cpu to be used, can be empty
        /// - features: llvm features string (features delimited by `,` with `+` for enable and `-` for disable), can be empty
        /// # Returns
        /// Ok() and UniquePtr holding a NyxstoneFFI on success, Err() otherwise.
        fn create_nyxstone_ffi(triple_name: &str, cpu: &str, features: &str, style: IntegerBase) -> NyxstoneResult;

        // Translates assembly instructions at a given start address to bytes.
        // Additional label definitions by absolute address may be supplied.
        // Does not support assembly directives that impact the layout (f. i., .section, .org).
        fn assemble_to_bytes(
            self: &NyxstoneFFI,
            assembly: &str,
            address: u64,
            labels: &[LabelDefinition],
        ) -> ByteResult;

        // Translates assembly instructions at a given start address to instruction details containing bytes.
        // Additional label definitions by absolute address may be supplied.
        // Does not support assembly directives that impact the layout (f. i., .section, .org).
        fn assemble_to_instructions(
            self: &NyxstoneFFI,
            assembly: &str,
            address: u64,
            labels: &[LabelDefinition],
        ) -> InstructionResult;

        // Translates bytes to disassembly text at given start address.
        fn disassemble_to_text(self: &NyxstoneFFI, bytes: &[u8], address: u64, count: usize) -> StringResult;

        // Translates bytes to instruction details containing disassembly text at a given start address.
        fn disassemble_to_instructions(
            self: &NyxstoneFFI,
            bytes: &[u8],
            address: u64,
            count: usize,
        ) -> InstructionResult;
    }
}

unsafe impl Send for ffi::NyxstoneFFI {}
