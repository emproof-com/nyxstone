#[cfg(test)]
mod tests {
    use anyhow::Result;
    use nyxstone::{Instruction, IntegerBase, LabelDefinition, Nyxstone, NyxstoneBuilder};
    use std::assert_eq;

    #[test]
    fn assembler_test() -> Result<()> {
        let nyxstone = NyxstoneBuilder::default().with_triple("x86_64-linux-gnu").build()?;

        let result = nyxstone.assemble_to_bytes("mov rax, rax", 0x1000, &[])?;
        assert_eq!(result, vec![0x48, 0x89, 0xc0]);

        Ok(())
    }

    #[test]
    fn disassembler_test() -> Result<()> {
        let nyxstone = NyxstoneBuilder::default().with_triple("x86_64-linux-gnu").build()?;

        let result = nyxstone.disassemble_to_text(&[0x48, 0x89, 0xc0], 0x1000, 0)?;
        assert_eq!(result, "mov rax, rax\n".to_owned());

        Ok(())
    }

    #[test]
    fn ldr_aligned_misaligned_armv6m_test() -> Result<()> {
        let nyxstone = NyxstoneBuilder::default().with_triple("armv6m-none-eabi").build()?;

        // 4 byte aligned instruction addresses
        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x00,
            &[LabelDefinition {
                name: ".label",
                address: 0x04,
            }],
        )?;
        assert_eq!(bytes, vec![0x00, 0x48]);

        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x00,
            &[LabelDefinition {
                name: ".label",
                address: 0x08,
            }],
        )?;
        assert_eq!(bytes, vec![0x01, 0x48]);

        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x00,
            &[LabelDefinition {
                name: ".label",
                address: 0x0c,
            }],
        )?;
        assert_eq!(bytes, vec![0x02, 0x48]);

        // 2 byte aligned instruction addresses
        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x02,
            &[LabelDefinition {
                name: ".label",
                address: 0x04,
            }],
        )?;
        assert_eq!(bytes, vec![0x00, 0x48]);

        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x02,
            &[LabelDefinition {
                name: ".label",
                address: 0x08,
            }],
        )?;
        assert_eq!(bytes, vec![0x01, 0x48]);

        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x02,
            &[LabelDefinition {
                name: ".label",
                address: 0x0c,
            }],
        )?;
        assert_eq!(bytes, vec![0x02, 0x48]);

        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label\nldr r0, .label",
            0x02,
            &[LabelDefinition {
                name: ".label",
                address: 0x0c,
            }],
        )?;
        assert_eq!(bytes, vec![0x02, 0x48, 0x01, 0x48]);

        // this should fail because the label target address for ldr instruction is not 4 byte aligned!
        let bytes = nyxstone.assemble_to_bytes(
            "ldr r0, .label",
            0x0a,
            &[LabelDefinition {
                name: ".label",
                address: 0x0e,
            }],
        );
        assert!(bytes.is_err());

        Ok(())
    }

    #[test]
    fn ldr_segfault_regression_test() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        // #8  assembly="ldr r3, .lbl_0x00c0f4\n.lbl_0x00c0f4:\n", address=49352,
        let result = nyxstone_armv8m.assemble_to_bytes(
            "ldr r3, .lbl_0x00c0f4",
            0xC0C8,
            &[LabelDefinition {
                name: ".lbl_0x00c0f4",
                address: 0xc0f4,
            }],
        )?;

        assert_eq!(result, vec![0x0a, 0x4b]);

        let result = nyxstone_armv8m.assemble_to_bytes(
            "ldr r3, .label",
            0,
            &[LabelDefinition {
                name: ".label",
                address: 4000,
            }],
        )?;

        assert_eq!(result, vec![0xDF, 0xF8, 0x9C, 0x3F]);

        Ok(())
    }

    #[test]
    fn nyxstone_test() -> Result<()> {
        let nyxstone_x86_64 = NyxstoneBuilder::default().with_triple("x86_64-linux-gnu").build()?;
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let labels = [LabelDefinition {
            name: ".label",
            address: 0x1010,
        }];

        let result = nyxstone_x86_64.assemble_to_bytes("mov rax, rax", 0x1000, &labels)?;
        assert_eq!(result, vec![0x48, 0x89, 0xc0]);

        let result = nyxstone_armv8m.assemble_to_bytes("bne .label", 0x1000, &labels)?;
        assert_eq!(result, vec![0x06, 0xd1]);

        let result = nyxstone_x86_64.assemble_to_instructions("mov rax, rax", 0x1000, &labels)?;
        assert_eq!(
            result,
            vec![Instruction {
                address: 0x1000,
                assembly: "mov rax, rax".into(),
                bytes: vec![0x48, 0x89, 0xc0]
            }]
        );

        let result = nyxstone_armv8m.assemble_to_instructions("bne .label", 0x1000, &labels)?;

        assert_eq!(
            result,
            vec![Instruction {
                address: 0x1000,
                assembly: "bne .label".into(),
                bytes: vec![0x06, 0xd1]
            }]
        );

        let result = nyxstone_x86_64.disassemble_to_instructions(&[0x48, 0x89, 0xc0], 0x1000, 0)?;

        assert_eq!(
            result,
            vec![Instruction {
                address: 0x1000,
                assembly: "mov rax, rax".into(),
                bytes: vec![0x48, 0x89, 0xc0]
            }]
        );

        let result = nyxstone_armv8m.disassemble_to_instructions(&[0x06, 0xd1], 0x1000, 0)?;

        assert_eq!(
            result,
            vec![Instruction {
                address: 0x1000,
                assembly: "bne #12".into(),
                bytes: vec![0x06, 0xd1]
            }]
        );

        Ok(())
    }

    #[test]
    fn nyxstone_armv8a_ldr_regression_test() -> Result<()> {
        let nyxstone_armv8a = NyxstoneBuilder::default()
            .with_triple("aarch64-linux-gnueabihf")
            .build()?;

        let result = nyxstone_armv8a.assemble_to_bytes(
            "ldr x10, .label",
            0x0,
            &[LabelDefinition {
                name: ".label".into(),
                address: 4000,
            }],
        )?;
        assert_eq!(result, vec![0x0A, 0x7D, 0x00, 0x58]);

        let _ = nyxstone_armv8a.assemble_to_instructions(
            "ldr x10, .label",
            0x0,
            &[LabelDefinition {
                name: ".label".into(),
                address: 4000,
            }],
        )?;

        Ok(())
    }

    #[test]
    fn nyxstone_armv8m_ldr_regression_test() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let result = nyxstone_armv8m.assemble_to_bytes(
            "ldr r3, .label",
            0x0,
            &[LabelDefinition {
                name: ".label".into(),
                address: 4000,
            }],
        )?;
        assert_eq!(result, vec![0xdf, 0xf8, 0x9c, 0x3f]);

        Ok(())
    }

    #[test]
    fn nyxstone_armv8a_regression_test_call_fixup() -> Result<()> {
        let nyxstone_armv8a = NyxstoneBuilder::default()
            .with_triple("aarch64-linux-gnueabihf")
            .build()?;

        let labels = [LabelDefinition {
            name: ".label",
            address: 0x1010,
        }];

        // test adr/adrp instructions
        let result = nyxstone_armv8a.assemble_to_bytes("adr x0, .label", 0x1000, &labels)?;
        // 0x0000000000001000:  80 00 00 10    adr x0, #0x1010
        // because pc + offset = 0x1000 + 0x10 = 0x1010
        assert_eq!(result, vec![0x80, 0x00, 0x00, 0x10], "assemble_to_bytes");

        let result = nyxstone_armv8a.assemble_to_instructions("adr x0, .label", 0x1000, &labels)?;
        assert_eq!(result.len(), 1);
        assert_eq!(
            result[0].bytes,
            vec![0x80, 0x00, 0x00, 0x10],
            "assemble_to_instructions"
        );

        let result = nyxstone_armv8a.assemble_to_bytes(
            "adrp x3, .label",
            0x1000,
            &[LabelDefinition {
                name: ".label",
                address: 0x3000,
            }],
        )?;
        assert_eq!(result, vec![0x03, 0x00, 0x00, 0xd0], "assemble_to_bytes");

        let result = nyxstone_armv8a.assemble_to_bytes(
            "adrp x3, .label",
            0x3000,
            &[LabelDefinition {
                name: ".label",
                address: 0x1000,
            }],
        )?;
        assert_eq!(result, vec![0xe3, 0xff, 0xff, 0xd0], "assemble_to_bytes");

        let result = nyxstone_armv8a.assemble_to_instructions(
            "adrp x3, .label",
            0x1000,
            &[LabelDefinition {
                name: ".label",
                address: 0x3000,
            }],
        )?;
        assert_eq!(result.len(), 1);
        assert_eq!(
            result[0].bytes,
            vec![0x03, 0x00, 0x00, 0xd0],
            "assemble_to_instructions"
        );

        let result = nyxstone_armv8a.assemble_to_bytes(
            "adrp x30, .label",
            0x1000,
            &[LabelDefinition {
                name: ".label",
                address: 0x2000,
            }],
        )?;
        assert_eq!(result, vec![0x1e, 0x00, 0x00, 0xb0], "assemble_to_bytes");

        let result = nyxstone_armv8a.assemble_to_bytes(
            "adrp x15, .label",
            0x1000,
            &[LabelDefinition {
                name: ".label",
                address: 0x1010,
            }],
        )?;
        assert_eq!(result, vec![0x0F, 0x00, 0x00, 0xb0], "assemble_to_bytes");

        Ok(())
    }

    #[test]
    fn nyxstone_regression_test_call_fixup() -> Result<()> {
        let nyxstone_x86_64 = NyxstoneBuilder::default().with_triple("x86_64-linux-gnu").build()?;
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let labels = [LabelDefinition {
            name: ".label",
            address: 0x1010,
        }];

        let result = nyxstone_armv8m.assemble_to_bytes("bl .label", 0x1000, &labels)?;
        assert_eq!(result, vec![0x00, 0xf0, 0x06, 0xf8], "assemble_to_bytes");

        let result = nyxstone_armv8m.assemble_to_instructions("bl .label", 0x1000, &labels)?;
        assert_eq!(result.len(), 1);
        assert_eq!(
            result[0].bytes,
            vec![0x00, 0xf0, 0x06, 0xf8],
            "assemble_to_instructions"
        );

        let result = nyxstone_x86_64.assemble_to_bytes("call .label", 0x1000, &labels)?;
        assert_eq!(result, vec![0xe8, 0x0b, 0x00, 0x00, 0x00], "assemble_to_bytes");

        let result = nyxstone_x86_64.assemble_to_instructions("call .label", 0x1000, &labels)?;
        assert_eq!(result.len(), 1);
        assert_eq!(
            result[0].bytes,
            vec![0xe8, 0x0b, 0x00, 0x00, 0x00],
            "assemble_to_instructions"
        );

        Ok(())
    }

    #[test]
    fn nyxstone_armv8m_regression_test_call_fixup() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let labels = [LabelDefinition {
            name: ".label",
            address: 0x1010,
        }];

        // test adr/adrp instructions
        let result = nyxstone_armv8m.assemble_to_bytes("adr r0, .label", 0x1000, &labels)?;
        // 0x0000000000001000:  80 00 00 10    adr x0, #0x1010
        // because pc + offset = 0x1000 + 0x10 = 0x1010
        assert_eq!(result, vec![0x03, 0xa0], "assemble_to_bytes");

        let result = nyxstone_armv8m.assemble_to_instructions("adr r0, .label", 0x1000, &labels)?;
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].bytes, vec![0x03, 0xa0], "assemble_to_instructions");

        let result = nyxstone_armv8m.assemble_to_instructions("adr r0, .label", 0x1020, &labels)?;
        assert_eq!(result.len(), 1);
        assert_eq!(
            result[0].bytes,
            vec![0xaf, 0xf2, 0x14, 0x00],
            "assemble_to_instructions"
        );

        Ok(())
    }

    #[test]
    fn nyxstone_isa() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .with_feature("fp16")
            .with_feature("mve.fp")
            .build()?;
        // test floating point instructions
        let bytes = nyxstone_armv8m.assemble_to_bytes("vadd.f16 s0, s1, s2", 0x1000, &[])?;
        assert_eq!(bytes, vec![0x30, 0xee, 0x81, 0x09]);

        let instructions = nyxstone_armv8m.disassemble_to_instructions(&bytes, 0x1000, 0)?;

        assert_eq!(
            instructions,
            vec![Instruction {
                address: 0x1000,
                assembly: "vadd.f16 s0, s1, s2".into(),
                bytes: vec![0x30, 0xee, 0x81, 0x09],
            }]
        );

        Ok(())
    }

    // TODO
    // We want to validate that setting a specific cpu enables its additional features
    // as well as validating that disabling the enabled features actually disables them...
    // #[test]
    // fn nyxstone_isa_cpu() -> Result<()> {
    //     let nyxstone = NyxstoneBuilder::default()
    //         .with_triple("some-triple")
    //         .with_cpu("some-cpu-with-feature-x")
    //         .without_feature("x")
    //         .build()?;
    //     // test feature x

    //     Ok(())
    // }

    #[test]
    fn nyxstone_isa_fails() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .with_feature("fp16")
            .without_feature("mve.fp")
            .build()?;

        // Test that floating point instructions fail if the feature is not enabled.
        let result = nyxstone_armv8m.assemble_to_bytes("vadd.f16 s0, s1, s2", 0x1000, &[]);
        assert!(result.is_err());

        let result = nyxstone_armv8m.disassemble_to_text(&[0x30, 0xee, 0x81, 0x09], 0x1000, 0);

        match result {
            Ok(instructions) => assert_ne!(instructions, "vadd.f16 s0, s1, s2".to_owned(),),
            Err(_) => {}
        }

        Ok(())
    }

    #[test]
    fn nyxstone_hex_printing() -> Result<()> {
        let bytes = [0x00, 0xf1, 0x0a, 0x00]; // add.w r0, r0, #10

        // Ensure that the default prints immediates as decimals.
        let nyxstone = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let asm = nyxstone.disassemble_to_text(&bytes, 0x0, 0)?;
        assert_eq!(asm, "add.w r0, r0, #10\n");

        // Ensure that HexPrefix works.
        let nyxstone = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .with_immediate_style(IntegerBase::HexPrefix)
            .build()?;

        let asm = nyxstone.disassemble_to_text(&bytes, 0x0, 0)?;
        assert_eq!(asm, "add.w r0, r0, #0xa\n");

        // Ensure that HexSuffix works.
        let nyxstone = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .with_immediate_style(IntegerBase::HexSuffix)
            .build()?;

        let asm = nyxstone.disassemble_to_text(&bytes, 0x0, 0)?;
        assert_eq!(asm, "add.w r0, r0, #0ah\n");

        Ok(())
    }

    #[test]
    fn armv8a_adr_out_of_range_regression() -> Result<()> {
        let nyxstone_armv8a = NyxstoneBuilder::default()
            .with_triple("aarch64-linux-gnueabihf")
            .build()?;

        let result = nyxstone_armv8a.assemble_to_bytes(
            "adr x21, .label",
            0x0,
            &[LabelDefinition {
                name: ".label",
                address: 0x100000,
            }],
        );

        assert!(result.is_err());

        Ok(())
    }

    #[test]
    fn armv8m_adr_out_of_range_regression() -> Result<()> {
        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .build()?;

        let result = nyxstone_armv8m.assemble_to_bytes(
            "adr r0, .label",
            0x0,
            &[LabelDefinition {
                name: ".label",
                address: 0x1010,
            }],
        );

        assert!(result.is_err());

        Ok(())
    }

    // The offset from a label is either taken from PC or Align(PC, 4).
    #[derive(PartialEq)]
    enum StartAddress {
        PC,
        AlignPC4,
    }

    // An offset to a label may need to be aligned (because the instruction does not encode the lower bits)
    type Alignment = Option<i64>;

    // Holds the information to check the validation checks for labels
    struct InstructionRange {
        instruction: &'static str,
        valid_range_encoding: [Option<([i64; 2], Alignment)>; 2], // One for 2-byte one for 4-byte encoding
        start: StartAddress,
    }
    impl InstructionRange {
        fn new(
            instruction: &'static str,
            valid_range_encoding: [Option<([i64; 2], Alignment)>; 2],
            start: StartAddress,
        ) -> Self {
            InstructionRange {
                instruction,
                valid_range_encoding,
                start,
            }
        }
    }

    // Checks if the nyxstone instance validates label offset for the given instructions.
    // Both the range of the offset and the alignment verification is checked. This function does
    // NOT verify that the generated bytecode is correct.
    fn check_instruction_ranges(nyxstone: &Nyxstone, instructions: Vec<InstructionRange>) -> Result<()> {
        let mut label = vec![LabelDefinition {
            name: ".label",
            address: 0,
        }];

        // Use an address big enough that address + offset is never < 0.
        let address = 0x10000000;

        for InstructionRange {
            instruction,
            valid_range_encoding,
            start,
        } in instructions
        {
            for (i, ranges) in valid_range_encoding.iter().enumerate() {
                if ranges.is_none() {
                    continue;
                }

                let (range, align) = ranges.unwrap();

                // The expected size for the given instruction, either 2-byte or 4-byte
                let instruction_size = (i + 1) * 2;

                // Check for two different addresses to make sure that we catch alignment issues
                for address_offset in (0..=2).step_by(2) {
                    let address = address + address_offset;
                    // Align the start address we are calculating the address for the label
                    // from, so that we can be sure whether a label distance is valid or not.
                    let start_address = if start == StartAddress::AlignPC4 {
                        (address + 4) & !0x3
                    } else {
                        address + 4
                    };

                    for (which, bound) in range.iter().enumerate() {
                        // Test -+4 byte around the upper and lower limit, so that we can be sure that offsets and alignment are checked.
                        for offset in -4..=4 {
                            // Depending on wether we are currently checking the lower or upper limit, we know if the offset is
                            // invalid when the offset is either < 0 or > 0.
                            let is_invalid_offset = if which == 0 { offset < 0 } else { offset > 0 };

                            // We calculate the offset from the start address to be the lower/upper bound + our testing offset
                            let offset = bound + offset;

                            label[0].address = (start_address as i64 + offset).try_into()?;

                            let result = nyxstone.assemble_to_bytes(instruction, address, &label);

                            let is_not_aligned = offset % align.unwrap_or(offset) != 0;

                            if is_invalid_offset || is_not_aligned {
                                // If the offset is invalid, i.e. the label is out of range, or the value is not aligned
                                // for the current encoding the instruction might have been assembled into the larger 4
                                // byte encoding (which might allow different alignments for labels), which we check for
                                // here, but only if we are currently checking the 2 byte encoding:
                                if instruction_size == 2 && valid_range_encoding[1].is_some() {
                                    if let Ok(ref bytes) = result {
                                        assert_ne!(bytes.len(), instruction_size,
                                        "Falsely assembled (= expected larger encoding ):{address:#x}: {instruction} (= {:#x}, offset = {offset})",
                                        label[0].address);
                                        continue;
                                    }
                                }
                                // For all other cases we can assume that assembling fails, since the label cannot be reached according to the ARM documentation.
                                assert!(
                                    result.is_err(),
                                    "Falsely assembled (= invalid offset: {is_invalid_offset}, invalid alignment: {is_not_aligned} ):\n{address:#x}: {instruction} (= {:#x}, offset = {offset})",
                                    label[0].address
                                );
                            } else {
                                // If the offset is valid, assembling must work. Normally, this test function fails because we are assembling invalid instructions, not because we are failing to assemble valid instructions.
                                assert!(result.is_ok(), "Unexpected error (= {:#?})", result.unwrap_err());

                                // Additionally we make sure that the assembled instruction has the expected size, to have some verification that we are assembling correctly.
                                assert_eq!(result.as_ref().unwrap().len(), instruction_size, "Falsely assembled (= expected different encoding ):{address:#x}: {instruction} (= {:#x}, offset = {offset}), {result:#x?}", label[0].address);
                            }
                        }
                    }
                }
            }
        }

        Ok(())
    }

    #[test]
    fn armv6m_check_label_range_validation() -> Result<()> {
        let instructions: Vec<InstructionRange> = vec![
            InstructionRange::new(
                "adr r0, .label",
                [Some(([0, 1020], Alignment::Some(4))), None],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "bne .label",
                [Some(([-256, 254], Alignment::Some(2))), None],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "b .label",
                [Some(([-2048, 2046], Alignment::Some(2))), None],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "bl .label",
                [None, Some(([-0x1000000, 0xfffffe], Alignment::Some(2)))],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "ldr r0, .label",
                [Some(([0, 1020], Alignment::Some(4))), None],
                StartAddress::AlignPC4,
            ),
        ];

        let nyxstone_armv6m = NyxstoneBuilder::default().with_triple("armv6m-none-eabi").build()?;

        check_instruction_ranges(&nyxstone_armv6m, instructions)?;

        Ok(())
    }

    #[test]
    fn armv7m_check_label_range_validation() -> Result<()> {
        let instructions: Vec<InstructionRange> = vec![
            InstructionRange::new(
                "adr r0, .label",
                [
                    Some(([0, 1020], Alignment::Some(4))),
                    Some(([-4095, 4095], Alignment::None)),
                ],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "b .label",
                [
                    Some(([-2048, 2046], Alignment::Some(2))),
                    Some(([-0x1000000, 0x0fffffe], Alignment::Some(2))),
                ],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "bne .label",
                [
                    Some(([-256, 254], Alignment::Some(2))),
                    Some(([-0x100000, 0x0ffffe], Alignment::Some(2))),
                ],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "bl .label",
                [None, Some(([-0x1000000, 0xfffffe], Alignment::Some(2)))],
                StartAddress::PC,
            ),
            // TODO: Since cbz r0, .label is assembled to a NOP for .label = PC + (2 or 3) it is difficult to check for
            //  the unaligned offset 3 in nyxstone. We might need a seperate test for this instruction :(
            // InstructionRange::new(
            //     "CBZ r0, .label",
            //     [Some(([-2, 126], Alignment::Some(2))), None], // We allow -2 here, since llvm transforms cbz r0, ".label\n.label:" to a nop.
            //     StartAddress::SP,
            // ),
            InstructionRange::new(
                "ldc p0, c0, .label",
                [None, Some(([-1020, 1020], Alignment::Some(4)))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "ldr r0, .label",
                [
                    Some(([0, 1020], Alignment::Some(4))),
                    Some(([-4095, 4095], Alignment::None)),
                ],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "ldrb r0, .label",
                [None, Some(([-4095, 4095], Alignment::None))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "pld .label",
                [None, Some(([-4095, 4095], Alignment::None))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "vldr d0, .label",
                [None, Some(([-1020, 1020], Alignment::Some(4)))],
                StartAddress::AlignPC4,
            ),
        ];

        let nyxstone_armv7m = NyxstoneBuilder::default()
            .with_triple("armv7m-none-eabi")
            .with_feature("fp16")
            .with_feature("mve.fp")
            .build()?;

        check_instruction_ranges(&nyxstone_armv7m, instructions)?;

        Ok(())
    }

    #[test]
    fn armv8m_check_label_range_validation() -> Result<()> {
        let instructions: Vec<InstructionRange> = vec![
            InstructionRange::new(
                "adr r0, .label",
                [
                    Some(([0, 1020], Alignment::Some(4))),
                    Some(([-4095, 4095], Alignment::None)),
                ],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "b .label",
                [
                    Some(([-2048, 2046], Alignment::Some(2))),
                    Some(([-0x1000000, 0x0fffffe], Alignment::Some(2))),
                ],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "bne .label",
                [
                    Some(([-256, 254], Alignment::Some(2))),
                    Some(([-0x100000, 0x0ffffe], Alignment::Some(2))),
                ],
                StartAddress::PC,
            ),
            InstructionRange::new(
                "bl .label",
                [None, Some(([-0x1000000, 0x0fffffe], Alignment::Some(2)))],
                StartAddress::PC,
            ),
            // TODO: Since cbz r0, .label is assembled to a NOP for .label = PC + (2 or 3) it is difficult to check for
            //  the unaligned offset 3 in nyxstone. We might need a seperate test for this instruction :(
            // InstructionRange::new(
            //     "CBZ r0, .label",
            //     [Some(([-2, 126], Alignment::Some(2))), None], // We allow -2 here, since llvm transforms cbz r0, ".label\n.label:" to a nop.
            //     StartAddress::SP,
            // ),
            InstructionRange::new(
                "ldc p0, c0, .label",
                [None, Some(([-1020, 1020], Alignment::Some(4)))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "ldr r0, .label",
                [
                    Some(([0, 1020], Alignment::Some(4))),
                    Some(([-4095, 4095], Alignment::None)),
                ],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "ldrb r0, .label",
                [None, Some(([-4095, 4095], Alignment::None))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "pld .label",
                [None, Some(([-4095, 4095], Alignment::None))],
                StartAddress::AlignPC4,
            ),
            InstructionRange::new(
                "vldr d0, .label",
                [None, Some(([-1020, 1020], Alignment::Some(4)))],
                StartAddress::AlignPC4,
            ),
        ];

        let nyxstone_armv8m = NyxstoneBuilder::default()
            .with_triple("armv8m.main-none-eabi")
            .with_feature("fp16")
            .with_feature("mve.fp")
            .build()?;

        check_instruction_ranges(&nyxstone_armv8m, instructions)?;
        Ok(())
    }
}

fn main() {}
