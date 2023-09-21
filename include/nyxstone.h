#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/TargetRegistry.h>
#pragma GCC diagnostic pop

class Nyxstone {
    /// The LLVM triple
    llvm::Triple triple;
    /// Specific cpu to use (can be empty).
    std::string m_cpu;
    /// Additional features to en-/disable (can be emtpy).
    std::string m_features;

    const llvm::Target& target;

    llvm::MCTargetOptions target_options;

    std::unique_ptr<llvm::MCRegisterInfo> register_info;
    std::unique_ptr<llvm::MCAsmInfo> assembler_info;
    std::unique_ptr<llvm::MCInstrInfo> instruction_info;
    std::unique_ptr<llvm::MCSubtargetInfo> subtarget_info;
    std::unique_ptr<llvm::MCInstPrinter> instruction_printer;

  public:
    /// Custom exception class used throughout nyxstone to pass any kind of String describing an error upwards.
    /// This exception is translated by rust_cxx into an Error.
    class Exception final: public std::exception {
      public:
        [[nodiscard]] const char* what() const noexcept override {
            return inner.c_str();
        }

        Exception(const Exception& error) noexcept : inner(error.inner) {}
        Exception(Exception&& error) noexcept : inner(std::move(error.inner)) {}
        explicit Exception(const char* desc) noexcept : inner(desc) {}
        explicit Exception(std::string&& desc) noexcept : inner(desc) {}
        explicit Exception(const llvm::StringRef& desc) noexcept : inner(desc.str()) {}
        ~Exception() override = default;

        Exception& operator=(const Exception& error) noexcept {
            if (this != &error) {
                this->inner = error.inner;
            }
            return *this;
        }

        Exception& operator=(Exception&& error) noexcept {
            this->inner = std::move(error.inner);
            return *this;
        }

      private:
        std::string inner;
    };

    // Defines the location of a label by absolute address.
    struct LabelDefinition {
        std::string name;
        uint64_t address;
    };

    // Instruction details
    struct Instruction {
        uint64_t address;
        std::string assembly;
        std::vector<uint8_t> bytes {};
    };

    /// @brief Nyxstone constructor called by NyxstoneBuilder::build.
    ///
    /// @warning This function should not be called directly, use NyxstoneBuilder instead.
    ///
    /// @param triple The llvm triple used to construct the llvm objects.
    /// @param cpu The cpu string used to construct the @p subtarget_info (as it points to the data inside).
    /// @param features The cpu features used to construct the @p subtarget_info (as it points to the data inside).
    /// @param target Target for the given triple.
    /// @param target_options Empty target options (reused from creating assembler_info).
    /// @param register_info Register info for the given triple.
    /// @param assembler_info Assembler info for given triple.
    /// @param instruction_info Instruction information for the given triple.
    /// @param subtarget_info Information about the subtarget, created with @p cpu and @p features.
    /// @param instruction_printer Instruction printer for the given architecture.
    Nyxstone(
        llvm::Triple&& triple,
        std::string&& cpu,
        std::string&& features,
        const llvm::Target& target,
        llvm::MCTargetOptions&& target_options,
        std::unique_ptr<llvm::MCRegisterInfo>&& register_info,
        std::unique_ptr<llvm::MCAsmInfo>&& assembler_info,
        std::unique_ptr<llvm::MCInstrInfo>&& instruction_info,
        std::unique_ptr<llvm::MCSubtargetInfo>&& subtarget_info,
        std::unique_ptr<llvm::MCInstPrinter>&& instruction_printer) noexcept :
        triple(std::move(triple)),
        m_cpu(std::move(cpu)),
        m_features(std::move(features)),
        target(target),
        target_options(std::move(target_options)),
        register_info(std::move(register_info)),
        assembler_info(std::move(assembler_info)),
        instruction_info(std::move(instruction_info)),
        subtarget_info(std::move(subtarget_info)),
        instruction_printer(std::move(instruction_printer)) {}

    // Translates assembly instructions at given start address to bytes.
    // Additional label definitions by absolute address may be supplied.
    // Does not support assembly directives that impact the layout (f. i., .section, .org).
    void assemble_to_bytes(
        const std::string& assembly,
        uint64_t address,
        const std::vector<LabelDefinition>& labels,
        std::vector<uint8_t>& bytes) const;

    // Translates assembly instructions at given start address to instruction details containing bytes.
    // Additional label definitions by absolute address may be supplied.
    // Does not support assembly directives that impact the layout (f. i., .section, .org).
    void assemble_to_instructions(
        const std::string& assembly,
        uint64_t address,
        const std::vector<LabelDefinition>& labels,
        std::vector<Instruction>& instructions) const;

    // Translates bytes to disassembly text at given start address.
    void
    disassemble_to_text(const std::vector<uint8_t>& bytes, uint64_t address, size_t count, std::string& disassembly)
        const;

    // Translates bytes to instruction details containing disassembly text at given start address.
    void disassemble_to_instructions(
        const std::vector<uint8_t>& bytes,
        uint64_t address,
        size_t count,
        std::vector<Instruction>& instructions) const;

  private:
    // Uses LLVM and MC to assemble instructions.
    // Utilizes some custom overloads to import user-supplied label definitions and extract instruction details.
    void assemble_impl(
        const std::string& assembly,
        uint64_t address,
        const std::vector<LabelDefinition>& labels,
        std::vector<uint8_t>& bytes,
        std::vector<Instruction>* instructions) const;

    // Uses LLVM and MC to disassemble instructions.
    void disassemble_impl(
        const std::vector<uint8_t>& bytes,
        uint64_t address,
        size_t count,
        std::string* disassembly,
        std::vector<Instruction>* instructions) const;
};

/**
 * @brief Builder for Nyxstone instances.
 **/
class NyxstoneBuilder {
  public:
    /// @brief Configuration options for the immediate representation in disassembly.
    enum class IntegerBase : uint8_t {
        /// Immediates are represented in decimal format.
        Dec = 0,
        /// Immediates are represented in hex format, prepended with 0x, for example: 0xff.
        HexPrefix = 1,
        /// Immediates are represented in hex format, suffixed with h, for example: 0ffh.
        HexSuffix = 2,
    };

  private:
    NyxstoneBuilder() = default;

    /// @brief Specific CPU for LLVM, default is empty.
    std::string m_cpu;
    /// @brief Specific CPU Features for LLVM, default is empty.
    std::string m_features;
    /// @brief In which style immediates should be represented in disassembly.
    IntegerBase m_imm_style = IntegerBase::Dec;

  public:
    NyxstoneBuilder(const NyxstoneBuilder&) = default;
    NyxstoneBuilder(NyxstoneBuilder&&) = delete;
    NyxstoneBuilder& operator=(const NyxstoneBuilder&) = default;
    NyxstoneBuilder& operator=(NyxstoneBuilder&&) = delete;

    ~NyxstoneBuilder() = default;

    /**
     * @brief Creates a NyxstoneBuilder instance with the default options for building the Nyxstone instance.
     */
    static NyxstoneBuilder Default() {
        return {};
    }

    /**
     * @brief Specifies the cpu for which to assemble/disassemble in nyxstone.
     * @return Reference to the updated NyxstoneBuilder object.
     */
    NyxstoneBuilder& with_cpu(std::string&& cpu);
    /**
     * @brief Specify cpu features to en-/disable in nyxstone.
     * @return Reference to the updated NyxstoneBuilder object.
     */
    NyxstoneBuilder& with_features(std::string&& features);

    /// @brief Specify the style in which immediates should be represented.
    ///
    /// @return Reference to the updated NyxstoneBuilder object.
    NyxstoneBuilder& with_immediate_style(IntegerBase style);

    /**
     * @brief Builds a nyxstone instance from the builder.
     * @warning Consumes the NyxstoneBuilder instance, using it after calling this function is unsafe.
     * @return A unique_ptr holding the created nyxstone instance.
     * @throws Nyxstone::Exception Thrown if initialization or object creation fails.
     */
    std::unique_ptr<Nyxstone> build(std::string&& triple);
};

/// Detects all ARM Thumb architectures. LLVM doesn't seem to have a short way to check this.
bool is_ArmT16_or_ArmT32(const llvm::Triple& triple);
