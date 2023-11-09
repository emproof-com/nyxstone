import nyxstone_cpp
from enum import IntEnum

class IntegerBase(IntEnum):
    Dec = nyxstone_cpp.NyxstoneBuilderFFI.IntegerBase.Dec
    HexPrefix = nyxstone_cpp.NyxstoneBuilderFFI.IntegerBase.HexPrefix
    HexSuffix = nyxstone_cpp.NyxstoneBuilderFFI.IntegerBase.HexSuffix

class Instruction(nyxstone_cpp.Instruction):
    @classmethod
    def _from_cpp_instruction(cls, instr: nyxstone_cpp.Instruction):
        return cls(instr.address, instr.assembly, instr.bytes)

class Nyxstone:
    def assemble_to_bytes(self, assembly: str, address: int = 0x0, labels: dict[str, int] = {}) -> list[int]:
        res = self.nyxstone.assemble_to_bytes(assembly, address, labels)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return res

    def assemble_to_instructions(self, assembly: str, address: int = 0x0, labels: dict[str, int] = {}) -> list[Instruction]:
        res = self.nyxstone.assemble_to_instructions(assembly, address, labels)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return list(map(Instruction._from_cpp_instruction, res))

    def disassemble_to_text(self, bytecode: list[int], address: int = 0x0, count: int = 0) -> str:
        res = self.nyxstone.disassemble_to_text(bytecode, address, count)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return res

    def disassemble_to_instructions(self, bytecode: list[int], address: int = 0x0, count: int = 0) -> list[Instruction]:
        res = self.nyxstone.disassemble_to_instructions(bytecode, address, count)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return list(map(Instruction._from_cpp_instruction, res))

    def __init__(self, use_the_builder_key, instance: nyxstone_cpp.NyxstoneFFI):
        if use_the_builder_key != NyxstoneBuilder._create_key:
            raise ValueError("Nyxstone must be created using the NyxstoneBuilder")
        self.nyxstone = instance


class NyxstoneBuilder(nyxstone_cpp.NyxstoneBuilderFFI):
    _create_key = object()

    def with_immediate_style(self, style: IntegerBase):
        return super().with_immediate_style(nyxstone_cpp.NyxstoneBuilderFFI.IntegerBase(style))

    def build(self) -> Nyxstone:
        res = super().build()
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return Nyxstone(NyxstoneBuilder._create_key, res)
