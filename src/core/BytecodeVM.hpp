#pragma once

#include "AST.hpp"
#include "SourceLocation.hpp"
#include "SymbolTable.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mathcompiler {

enum class OpCode {
    PUSH,
    LOAD,
    STORE,
    DEF_FUNC,
    ADD,
    SUB,
    MUL,
    DIV,
    POW,
    MOD,
    NEG,
    POS,
    CALL,
    POP,
    HALT
};

[[nodiscard]] const char* opCodeName(OpCode op) noexcept;

struct BytecodeInstruction {
    std::size_t address = 0;
    OpCode op = OpCode::HALT;
    double immediate = 0.0;
    std::string symbol;
    std::size_t argCount = 0;
    std::vector<std::string> funcParams;
    std::shared_ptr<const ASTNode> funcBody;
    std::shared_ptr<const ASTNode> deferredAst;  // Used for ndiff(expr, var, at)
    SourceSpan span{};
};

struct BytecodeProgram {
    std::vector<BytecodeInstruction> instructions;

    [[nodiscard]] std::string disassemble() const;
    [[nodiscard]] static std::string formatInstructionOperand(const BytecodeInstruction& ins);
};

/// Compiles an optimized AST into stack-based bytecode instructions.
class BytecodeCompiler {
public:
    [[nodiscard]] static BytecodeProgram compile(const ASTNode& root);
};

/// High-performance stack-based Virtual Machine with call-depth guard and unwind diagnostics.
class BytecodeVM {
public:
    static constexpr std::size_t kMaxCallDepth = 32;

    [[nodiscard]] double execute(const BytecodeProgram& program,
                                 SymbolTable& symbols,
                                 const std::unordered_map<std::string, double>* locals = nullptr,
                                 std::size_t callDepth = 0) const;

    /// Fast execution path with a single bound variable (e.g. `x` during 2D function plotting).
    [[nodiscard]] double executeWithSingleParam(const BytecodeProgram& program,
                                                const SymbolTable& symbols,
                                                std::string_view paramName,
                                                double paramValue) const;
};

}  // namespace mathcompiler
