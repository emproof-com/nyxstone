#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <variant>

#include "nyxstone.h"

namespace py = pybind11;

using namespace nyxstone;

class NyxstoneError {
public:
    std::string err;
};

std::vector<Nyxstone::LabelDefinition> convert_labels(std::unordered_map<std::string, uint64_t>&& labels)
{
    std::vector<Nyxstone::LabelDefinition> vlabels {};
    vlabels.reserve(labels.size());

    for (auto [label, address] : labels) {
        vlabels.push_back(Nyxstone::LabelDefinition { std::move(label), address });
    }

    return vlabels;
}

std::variant<std::vector<uint8_t>, NyxstoneError> assemble(
    Nyxstone& nyxstone, std::string assembly, uint64_t address, std::unordered_map<std::string, uint64_t> labels)
{
    auto vlabels = convert_labels(std::move(labels));

    auto res = nyxstone.assemble(assembly, address, vlabels);

    if (!res) {
        return NyxstoneError { std::move(res.error()) };
    }

    return res.value();
}

std::variant<std::vector<Nyxstone::Instruction>, NyxstoneError> assemble_to_instructions(
    Nyxstone& nyxstone, std::string assembly, uint64_t address, std::unordered_map<std::string, uint64_t> labels)
{
    auto vlabels = convert_labels(std::move(labels));

    auto res = nyxstone.assemble_to_instructions(assembly, address, vlabels);

    if (!res) {
        return NyxstoneError { std::move(res.error()) };
    }

    return res.value();
}

std::variant<std::string, NyxstoneError> disassemble(
    Nyxstone& nyxstone, std::vector<uint8_t> bytes, uint64_t address, uint64_t count)
{
    auto res = nyxstone.disassemble(bytes, address, count);

    if (!res) {
        return NyxstoneError { std::move(res.error()) };
    }

    return res.value();
}

std::variant<std::vector<Nyxstone::Instruction>, NyxstoneError> disassemble_to_instructions(
    Nyxstone& nyxstone, std::vector<uint8_t> bytes, uint64_t address, uint64_t count)
{
    auto res = nyxstone.disassemble_to_instructions(bytes, address, count);

    if (!res) {
        return NyxstoneError { std::move(res.error()) };
    }

    return res.value();
}

std::variant<std::unique_ptr<Nyxstone>, NyxstoneError> create_nyxstone(
    std::string&& triple, std::string&& cpu, std::string&& features, NyxstoneBuilder::IntegerBase immediate_style)
{

    auto res = NyxstoneBuilder(std::move(triple))
                   .with_cpu(std::move(cpu))
                   .with_features(std::move(features))
                   .with_immediate_style(immediate_style)
                   .build();

    if (!res) {
        return NyxstoneError { res.error() };
    }

    return std::move(res.value());
}

PYBIND11_MODULE(nyxstone_cpp, m)
{
    m.doc() = "pybind11 plugin for nyxstone";

    py::class_<Nyxstone::Instruction>(m, "Instruction")
        .def(py::init<uint64_t, std::string, std::vector<uint8_t>>(), py::arg("address"), py::arg("assembly"),
            py::arg("bytes"))
        .def_readwrite("address", &Nyxstone::Instruction::address, "The address of the instruction")
        .def_readwrite("bytes", &Nyxstone::Instruction::bytes, "The assembled bytes of the instruction")
        .def_readwrite("assembly", &Nyxstone::Instruction::assembly, "The assembly of the instruction")
        .def("__eq__",
            [](const Nyxstone::Instruction& self, const Nyxstone::Instruction& other) { return self == other; })
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

    py::enum_<NyxstoneBuilder::IntegerBase>(m, "IntegerBase")
        .value("Dec", NyxstoneBuilder::IntegerBase::Dec, "Decimal printing")
        .value("HexPrefix", NyxstoneBuilder::IntegerBase::HexPrefix, "Hex, prefixed with '0x'")
        .value("HexSuffix", NyxstoneBuilder::IntegerBase::HexSuffix, "Hex, suffixed with 'h'");

    py::class_<NyxstoneError>(m, "NyxstoneError").def(py::init()).def_readwrite("err", &NyxstoneError::err);

    py::class_<Nyxstone>(m, "NyxstoneFFI")
        .def("assemble", &assemble, py::arg("assembly"), py::arg("address") = 0x0, py::arg("labels") = py::dict {})
        .def("assemble_to_instructions", &assemble_to_instructions, py::arg("assembly"), py::arg("address") = 0x0,
            py::arg("labels") = py::dict {})
        .def("disassemble", &disassemble, py::arg("bytes"), py::arg("address") = 0x0, py::arg("count") = 0,
            "Disassemble bytes to assembly text.\n"
            "count specifies the number of instructions to disassemble, '0' means all instructions")
        .def("disassemble_to_instructions", &disassemble_to_instructions, py::arg("bytes"), py::arg("address") = 0x0,
            py::arg("count") = 0x0,
            "Disassemble bytes to instruction information.\n"
            "count specifies the number of instructions to disassemble, '0' means all instructions");

    m.def("create_nyxstone", &create_nyxstone, py::arg("target_triple"), py::arg("cpu") = "", py::arg("features") = "",
        py::arg("immediate_style") = NyxstoneBuilder::IntegerBase::Dec, "Create a NyxstoneFFI instance");
}
