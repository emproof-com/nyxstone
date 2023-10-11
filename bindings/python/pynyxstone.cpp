#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <sstream>
#include <iomanip>

#include <nyxstone.h>

namespace py = pybind11;

std::vector<uint8_t> assemble_to_bytes(
    Nyxstone& nyxstone,
    std::string assembly,
    uint64_t address,
    std::vector<Nyxstone::LabelDefinition> labels) {
    std::vector<uint8_t> bytes;
    nyxstone.assemble_to_bytes(assembly, address, labels, bytes);
    return bytes;
}

std::vector<Nyxstone::Instruction> assemble_to_instructions(
    Nyxstone& nyxstone,
    std::string assembly,
    uint64_t address,
    std::vector<Nyxstone::LabelDefinition> labels) {
    std::vector<Nyxstone::Instruction> instructions;
    nyxstone.assemble_to_instructions(assembly, address, labels, instructions);
    return instructions;
}

std::string disassemble_to_text(Nyxstone& nyxstone, std::vector<uint8_t> bytes, uint64_t address, uint64_t count) {
    std::string assembly;
    nyxstone.disassemble_to_text(bytes, address, count, assembly);
    return assembly;
}

std::vector<Nyxstone::Instruction>
disassemble_to_instructions(Nyxstone& nyxstone, std::vector<uint8_t> bytes, uint64_t address, uint64_t count) {
    std::vector<Nyxstone::Instruction> instructions;
    nyxstone.disassemble_to_instructions(bytes, address, count, instructions);
    return instructions;
}

PYBIND11_MODULE(nyxstone, m) {
    m.doc() = "pybind11 plugin for nyxstone";

    py::class_<Nyxstone::LabelDefinition>(m, "LabelDefinition")
        .def(py::init<std::string, uint64_t>(), py::arg("name"), py::arg("address"))
        .def_readwrite("name", &Nyxstone::LabelDefinition::name)
        .def_readwrite("address", &Nyxstone::LabelDefinition::address);

    py::class_<Nyxstone::Instruction>(m, "Instruction")
        .def(
            py::init<uint64_t, std::string, std::vector<uint8_t>>(),
            py::arg("address"),
            py::arg("assembly"),
            py::arg("bytes"))
        .def_readwrite("address", &Nyxstone::Instruction::address)
        .def_readwrite("bytes", &Nyxstone::Instruction::bytes)
        .def_readwrite("assembly", &Nyxstone::Instruction::assembly)
        .def(
            "__eq__",
            [](const Nyxstone::Instruction& self, const Nyxstone::Instruction& other) {
                return self.address == other.address && self.assembly == other.assembly && self.bytes == other.bytes;
            })
        .def("__repr__", [](const Nyxstone::Instruction& i) {
            std::stringstream out;
            out << "<address: 0x" << std::hex << std::setw(8) << std::setfill('0') << i.address << ", assembly: \""
                << i.assembly << "\", bytes: [ ";
            for (const auto& b : i.bytes) {
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<uint32_t>(b) << " ";
            }
            out << "]>";
            return out.str();
        });

    py::class_<NyxstoneBuilder> builder(m, "NyxstoneBuilder");

    py::enum_<NyxstoneBuilder::IntegerBase>(builder, "IntegerBase")
        .value("Dec", NyxstoneBuilder::IntegerBase::Dec, "Decimal printing")
        .value("HexPrefix", NyxstoneBuilder::IntegerBase::HexPrefix, "Hex, prefixed with '0x'")
        .value("HexSuffix", NyxstoneBuilder::IntegerBase::HexSuffix, "Hex, suffixed with 'h'");

    builder.def(py::init())
        .def("with_triple", &NyxstoneBuilder::with_triple, "Specify the llvm target triple")
        .def("with_features", &NyxstoneBuilder::with_features, "Specify the llvm features to be en- or disabled")
        .def("with_cpu", &NyxstoneBuilder::with_cpu, "Specify the cpu to use")
        .def(
            "with_immediate_style",
            &NyxstoneBuilder::with_immediate_style,
            "Specify the style in which immediates are printed")
        .def("build", &NyxstoneBuilder::build, "Build the Nyxstone instance");

    py::class_<Nyxstone>(m, "Nyxstone")
        .def("assemble_to_bytes", &assemble_to_bytes, py::arg("assembly"), py::arg("address"), py::arg("labels"))
        .def(
            "assemble_to_instructions",
            &assemble_to_instructions,
            py::arg("assembly"),
            py::arg("address"),
            py::arg("labels"))
        .def(
            "disassemble_to_instructions",
            &disassemble_to_instructions,
            py::arg("bytes"),
            py::arg("address"),
            py::arg("count"))
        .def("disassemble_to_text", &disassemble_to_text, py::arg("bytes"), py::arg("address"), py::arg("count"));
}
