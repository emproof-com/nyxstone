import nyxstone_cpp
from enum import IntEnum


class IntegerBase(IntEnum):
    """Styles for immediate printing in Nyxstone.

    Members
    -------
    Dec:
        Decimal printing
    HexPrefix:
        Hex, prefixed with '0x'
    HexSuffix:
        Hex, suffixed with 'h'
    """

    Dec = nyxstone_cpp.IntegerBase.Dec
    HexPrefix = nyxstone_cpp.IntegerBase.HexPrefix
    HexSuffix = nyxstone_cpp.IntegerBase.HexSuffix


class Instruction(nyxstone_cpp.Instruction):
    """
    Extended information of an Instruction.
    """

    @classmethod
    def _from_cpp_instruction(cls, instr: nyxstone_cpp.Instruction):
        return cls(instr.address, instr.assembly, instr.bytes)


class Nyxstone:
    """
    Nyxstone class used for assembling and disassembling for a given architecture.
    """

    def __init__(
        self,
        target_triple: str,
        cpu: str = "",
        features: str = "",
        immediate_style: IntegerBase = IntegerBase.Dec,
    ):
        """
        Parameters
        ----------
        target_triple : str
            Llvm target triple or architecture identifier of triple.
        cpu : str, optional
            Llvm CPU specifier (defaults to no specific CPU).
        features : str, optional
            Llvm feature string. The feature string is a comma seperated list of
            features which are prepended with a '+' for enabling and a '-' for disabling (defaults to no features).
        immediate_style : IntegerBase, optional
            Printing style of immediates in disassembly and extended instruction details.

        Raises
        ------
        ValueError
            If the creation of the nyxstone instance failed.
        """

        res = nyxstone_cpp.create_nyxstone(
            target_triple, cpu, features, nyxstone_cpp.IntegerBase(immediate_style)
        )
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        self.nyxstone = res

    def assemble(
        self, assembly: str, address: int = 0x0, labels: dict[str, int] = {}
    ) -> list[int]:
        """Translates assembly instructions at a given start address to bytes.

        Additional label definitions by absolute address may be supplied.
        Does not support assembly directives that impact the layout (f. i., .section, .org).

        Parameters
        ----------
        assembly : str
            The assembly instruction(s) to be assembled.
        address : int, optional
            The absolute address of the first instruction (default is 0x0).
        labels : dict[str, int], optional
            Label definitions, should hold all external labels used in assembly (default is an empty dict).

        Raises
        ------
        ValueError
            If assembling failed.

        Returns
        -------
        list[int]
            A list of 8-bit integers representing the assembled byte code.
        """

        res = self.nyxstone.assemble(assembly, address, labels)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return res

    def assemble_to_instructions(
        self, assembly: str, address: int = 0x0, labels: dict[str, int] = {}
    ) -> list[Instruction]:
        """Translates assembly instructions at given start address to instruction details containing bytes.

        Additional label definitions by absolute address may be supplied.
        Does not support assembly directives that impact the layout (f. i., .section, .org).

        Parameters
        ----------
        assembly : str
            The assembly instruction(s) to be assembled.
        address : int, optional
            The absolute address of the first instruction (default is 0x0).
        labels : dict[str, int], optional
            Label definitions, should hold all external labels used in assembly (default is an empty dict).

        Raises
        ------
        ValueError
            If assembling failed.

        Returns
        -------
        list[Instruction]
            A list of instruction details.
        """

        res = self.nyxstone.assemble_to_instructions(assembly, address, labels)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return list(map(Instruction._from_cpp_instruction, res))

    def disassemble(
        self, bytecode: list[int], address: int = 0x0, count: int = 0
    ) -> str:
        """Translates bytes to disassembly text at given start address.

        Parameters
        ----------
        bytes : list[int]
            The byte code to be disassembled.
        address : int, optional
            The absolute address of the byte code (default is 0x0).
        count : int, optional
            The number of instructions which should be disassembled, 0 means all (default is 0).

        Raises
        ------
        ValueError
            If disassembling failed.

        Returns
        -------
        str
            The disassembled text.
        """

        res = self.nyxstone.disassemble(bytecode, address, count)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return res

    def disassemble_to_instructions(
        self, bytecode: list[int], address: int = 0x0, count: int = 0
    ) -> list[Instruction]:
        """Translates bytes to instruction details containing disassembly text at given start address.

        Parameters
        ----------
        bytes : list[int]
            The byte code to be disassembled.
        address : int, optional
            The absolute address of the byte code (default is 0x0).
        count : int, optional
            The number of instructions which should be disassembled, 0 means all (default is 0).

        Raises
        ------
        ValueError
            If disassembling failed.

        Returns
        -------
        list[Instruction]
            A list of instruction details.
        """

        res = self.nyxstone.disassemble_to_instructions(bytecode, address, count)
        if isinstance(res, nyxstone_cpp.NyxstoneError):
            raise ValueError(res.err)
        return list(map(Instruction._from_cpp_instruction, res))
