#include "BytecodeVM.hpp"

#include "CompilerException.hpp"
#include "FunctionRegistry.hpp"
#include "MathOps.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mathcompiler {

const char* opCodeName(OpCode op) noexcept {
    switch (op) {
        case OpCode::PUSH:     return "PUSH";
        case OpCode::LOAD:     return "LOAD";
        case OpCode::STORE:    return "STORE";
        case OpCode::DEF_FUNC: return "DEF_FUNC";
        case OpCode::ADD:      return "ADD";
        case OpCode::SUB:      return "SUB";
        case OpCode::MUL:      return "MUL";
        case OpCode::DIV:      return "DIV";
        case OpCode::POW:      return "POW";
        case OpCode::MOD:      return "MOD";
        case OpCode::NEG:      return "NEG";
        case OpCode::POS:      return "POS";
        case OpCode::CALL:     return "CALL";
        case OpCode::POP:      return "POP";
        case OpCode::HALT:     return "HALT";
    }
    return "UNKNOWN";
}

std::string BytecodeProgram::formatInstructionOperand(const BytecodeInstruction& ins) {
    switch (ins.op) {
        case OpCode::PUSH:
            return formatCompactNumber(ins.immediate);
        case OpCode::LOAD:
        case OpCode::STORE:
            return ins.symbol;
        case OpCode::DEF_FUNC: {
            std::ostringstream oss;
            oss << ins.symbol << "(";
            for (std::size_t i = 0; i < ins.funcParams.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << ins.funcParams[i];
            }
            oss << ")";
            return oss.str();
        }
        case OpCode::CALL:
            return ins.symbol + ", " + std::to_string(ins.argCount);
        default:
            return {};
    }
}

std::string BytecodeProgram::disassemble() const {
    std::ostringstream oss;
    for (const auto& ins : instructions) {
        oss << std::setw(4) << std::setfill('0') << ins.address << "  "
            << std::left << std::setw(9) << std::setfill(' ') << opCodeName(ins.op);
        const std::string operand = formatInstructionOperand(ins);
        if (!operand.empty()) {
            oss << " " << operand;
        }
        oss << "\n";
    }
    return oss.str();
}

namespace {

void emitAst(const ASTNode& node, std::vector<BytecodeInstruction>& out) {
    auto emit = [&](BytecodeInstruction ins) {
        ins.address = out.size();
        out.push_back(std::move(ins));
    };

    switch (node.kind()) {
        case ASTNodeKind::Number: {
            const auto& n = static_cast<const NumberNode&>(node);
            BytecodeInstruction ins;
            ins.op = OpCode::PUSH;
            ins.immediate = n.value();
            ins.span = n.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::Variable: {
            const auto& v = static_cast<const VariableNode&>(node);
            BytecodeInstruction ins;
            ins.op = OpCode::LOAD;
            ins.symbol = v.name();
            ins.span = v.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::UnaryOp: {
            const auto& u = static_cast<const UnaryOpNode&>(node);
            emitAst(u.operand(), out);
            BytecodeInstruction ins;
            ins.op = (u.op() == UnaryOpKind::Negate) ? OpCode::NEG : OpCode::POS;
            ins.span = u.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::BinaryOp: {
            const auto& b = static_cast<const BinaryOpNode&>(node);
            emitAst(b.left(), out);
            emitAst(b.right(), out);
            BytecodeInstruction ins;
            switch (b.op()) {
                case BinaryOpKind::Add: ins.op = OpCode::ADD; break;
                case BinaryOpKind::Sub: ins.op = OpCode::SUB; break;
                case BinaryOpKind::Mul: ins.op = OpCode::MUL; break;
                case BinaryOpKind::Div: ins.op = OpCode::DIV; break;
                case BinaryOpKind::Pow: ins.op = OpCode::POW; break;
                case BinaryOpKind::Mod: ins.op = OpCode::MOD; break;
            }
            ins.span = b.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::FunctionCall: {
            const auto& f = static_cast<const FunctionCallNode&>(node);
            if (f.funcName() == "ndiff") {
                if (f.args().size() != 3) {
                    throw ArityMismatchException("ndiff", 3, f.args().size(), f.span());
                }
                if (f.args()[1]->kind() != ASTNodeKind::Variable) {
                    throw SemanticException(
                        "E_SYMBOLIC_DIFF",
                        "Second argument of ndiff(expr, var, at) must be a variable identifier",
                        f.args()[1]->span());
                }
                emitAst(*f.args()[2], out);
                BytecodeInstruction ins;
                ins.op = OpCode::CALL;
                ins.symbol = "ndiff";
                ins.argCount = 1;
                ins.funcParams = {static_cast<const VariableNode&>(*f.args()[1]).name()};
                ins.deferredAst = f.args()[0]->clone();
                ins.span = f.span();
                emit(std::move(ins));
                return;
            }

            if (f.funcName() == "diff") {
                if (f.args().size() != 2) {
                    throw ArityMismatchException("diff", 2, f.args().size(), f.span());
                }
                if (f.args()[1]->kind() != ASTNodeKind::Variable) {
                    throw SemanticException(
                        "E_SYMBOLIC_DIFF",
                        "Second argument of diff(expr, var) must be a variable identifier",
                        f.args()[1]->span());
                }
                const auto& varName = static_cast<const VariableNode&>(*f.args()[1]).name();
                auto lowered = SymbolicDiff::differentiate(*f.args()[0], varName);
                emitAst(*lowered, out);
                return;
            }

            for (const auto& arg : f.args()) {
                emitAst(*arg, out);
            }
            BytecodeInstruction ins;
            ins.op = OpCode::CALL;
            ins.symbol = f.funcName();
            ins.argCount = f.args().size();
            ins.span = f.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::Assign: {
            const auto& a = static_cast<const AssignNode&>(node);
            emitAst(a.rhs(), out);
            BytecodeInstruction ins;
            ins.op = OpCode::STORE;
            ins.symbol = a.targetName();
            ins.span = a.span();
            emit(std::move(ins));
            return;
        }

        case ASTNodeKind::FunctionDef: {
            const auto& fd = static_cast<const FunctionDefNode&>(node);
            BytecodeInstruction ins;
            ins.op = OpCode::DEF_FUNC;
            ins.symbol = fd.funcName();
            ins.funcParams = fd.params();
            ins.funcBody = fd.body().clone();
            ins.span = fd.span();
            emit(std::move(ins));
            return;
        }
    }
}

double runVmCore(const BytecodeProgram& program,
                 SymbolTable* mutableSymbols,
                 const SymbolTable& readSymbols,
                 const std::unordered_map<std::string, double>* locals,
                 std::size_t callDepth);

double invokeNdiff(const BytecodeInstruction& ins,
                   double x0,
                   const SymbolTable& readSymbols,
                   const std::unordered_map<std::string, double>* locals,
                   std::size_t callDepth) {
    const double h = 1e-6 * std::max(1.0, std::fabs(x0));
    const BytecodeProgram exprProg = BytecodeCompiler::compile(*ins.deferredAst);

    std::unordered_map<std::string, double> framePlus =
        locals ? *locals : std::unordered_map<std::string, double>{};
    framePlus[ins.funcParams[0]] = x0 + h;

    std::unordered_map<std::string, double> frameMinus =
        locals ? *locals : std::unordered_map<std::string, double>{};
    frameMinus[ins.funcParams[0]] = x0 - h;

    try {
        const double fPlus = runVmCore(exprProg, nullptr, readSymbols, &framePlus, callDepth + 1);
        const double fMinus = runVmCore(exprProg, nullptr, readSymbols, &frameMinus, callDepth + 1);
        const double deriv = (fPlus - fMinus) / (2.0 * h);
        if (!std::isfinite(deriv)) {
            throw MathDomainException("Numerical derivative diverged at evaluation point", ins.span);
        }
        return deriv == 0.0 ? 0.0 : deriv;
    } catch (CompilerException& ex) {
        ex.addFrame("VM Call ndiff()", ins.span);
        throw;
    }
}

double invokeUserFunction(const BytecodeInstruction& ins,
                          const std::vector<double>& args,
                          const SymbolTable& readSymbols,
                          std::size_t callDepth) {
    auto userFn = readSymbols.getFunction(ins.symbol);
    if (!userFn.has_value()) {
        throw UnknownFunctionException(ins.symbol, ins.span);
    }
    if (args.size() != userFn->params.size()) {
        throw ArityMismatchException(ins.symbol, userFn->params.size(), args.size(), ins.span);
    }

    std::unordered_map<std::string, double> fnLocals;
    fnLocals.reserve(userFn->params.size());
    for (std::size_t i = 0; i < userFn->params.size(); ++i) {
        fnLocals[userFn->params[i]] = args[i];
    }

    try {
        const BytecodeProgram fnProg = BytecodeCompiler::compile(*userFn->bodyAst);
        return runVmCore(fnProg, nullptr, readSymbols, &fnLocals, callDepth + 1);
    } catch (CompilerException& ex) {
        ex.addFrame("VM Call '" + ins.symbol + "()'", ins.span);
        throw;
    }
}

double runVmCore(const BytecodeProgram& program,
                 SymbolTable* mutableSymbols,
                 const SymbolTable& readSymbols,
                 const std::unordered_map<std::string, double>* locals,
                 std::size_t callDepth) {
    if (callDepth > BytecodeVM::kMaxCallDepth) {
        const SourceSpan sp = program.instructions.empty() ? SourceSpan{} : program.instructions.front().span;
        throw RuntimeEvalException(
            "E_STACK_OVERFLOW",
            "Maximum function call recursion depth exceeded",
            sp,
            "Check for cyclic user function definitions (e.g. f(x) = f(x)).");
    }

    std::vector<double> stack;
    stack.reserve(16);

    auto popValue = [&stack](const SourceSpan& sp) -> double {
        if (stack.empty()) {
            throw RuntimeEvalException("E_VM_UNDERFLOW", "VM stack underflow", sp);
        }
        const double v = stack.back();
        stack.pop_back();
        return v;
    };

    const BytecodeInstruction* activeIns = nullptr;
    try {
        for (const auto& ins : program.instructions) {
            activeIns = &ins;
            switch (ins.op) {
                case OpCode::PUSH:
                    stack.push_back(ins.immediate);
                    break;

                case OpCode::LOAD: {
                    if (locals) {
                        auto it = locals->find(ins.symbol);
                        if (it != locals->end()) {
                            stack.push_back(it->second);
                            break;
                        }
                    }
                    auto val = readSymbols.get(ins.symbol);
                    if (!val.has_value()) {
                        throw UndeclaredVariableException(ins.symbol, ins.span);
                    }
                    stack.push_back(*val);
                    break;
                }

                case OpCode::STORE: {
                    if (!mutableSymbols) {
                        throw RuntimeEvalException(
                            "E_READONLY_EVAL",
                            "Assignments are not allowed in read-only evaluation contexts",
                            ins.span);
                    }
                    const double val = popValue(ins.span);
                    if (!mutableSymbols->set(ins.symbol, val)) {
                        throw SemanticException(
                            "E_RESERVED_IDENTIFIER",
                            "Cannot assign to '" + ins.symbol + "'",
                            ins.span);
                    }
                    stack.push_back(val);
                    break;
                }

                case OpCode::DEF_FUNC: {
                    if (!mutableSymbols) {
                        throw RuntimeEvalException(
                            "E_READONLY_EVAL",
                            "Function definitions are not allowed in read-only evaluation contexts",
                            ins.span);
                    }
                    if (!mutableSymbols->setFunction(ins.symbol, ins.funcParams, ins.funcBody)) {
                        throw SemanticException(
                            "E_RESERVED_IDENTIFIER",
                            "Cannot redefine built-in function '" + ins.symbol + "()'",
                            ins.span);
                    }
                    stack.push_back(0.0);
                    break;
                }

                case OpCode::NEG: {
                    const double a = popValue(ins.span);
                    stack.push_back(applyUnaryOp(UnaryOpKind::Negate, a, ins.span));
                    break;
                }

                case OpCode::POS: {
                    const double a = popValue(ins.span);
                    stack.push_back(applyUnaryOp(UnaryOpKind::Plus, a, ins.span));
                    break;
                }

                case OpCode::ADD:
                case OpCode::SUB:
                case OpCode::MUL:
                case OpCode::DIV:
                case OpCode::POW:
                case OpCode::MOD: {
                    const double rhs = popValue(ins.span);
                    const double lhs = popValue(ins.span);
                    BinaryOpKind opKind = BinaryOpKind::Add;
                    switch (ins.op) {
                        case OpCode::ADD: opKind = BinaryOpKind::Add; break;
                        case OpCode::SUB: opKind = BinaryOpKind::Sub; break;
                        case OpCode::MUL: opKind = BinaryOpKind::Mul; break;
                        case OpCode::DIV: opKind = BinaryOpKind::Div; break;
                        case OpCode::POW: opKind = BinaryOpKind::Pow; break;
                        case OpCode::MOD: opKind = BinaryOpKind::Mod; break;
                        default: break;
                    }
                    stack.push_back(applyBinaryOp(opKind, lhs, rhs, ins.span));
                    break;
                }

                case OpCode::CALL: {
                    if (ins.symbol == "ndiff" && ins.deferredAst && !ins.funcParams.empty()) {
                        const double x0 = popValue(ins.span);
                        stack.push_back(invokeNdiff(ins, x0, readSymbols, locals, callDepth));
                        break;
                    }

                    std::vector<double> args(ins.argCount);
                    for (std::size_t i = ins.argCount; i > 0; --i) {
                        args[i - 1] = popValue(ins.span);
                    }

                    if (FunctionRegistry::isBuiltIn(ins.symbol)) {
                        stack.push_back(applyMathFunction(ins.symbol, args, ins.span));
                    } else {
                        stack.push_back(invokeUserFunction(ins, args, readSymbols, callDepth));
                    }
                    break;
                }

                case OpCode::POP:
                    (void)popValue(ins.span);
                    break;

                case OpCode::HALT:
                    return stack.empty() ? 0.0 : stack.back();
            }
        }
    } catch (CompilerException& ex) {
        if (ex.callStack().empty() && activeIns) {
            ex.addFrame(
                std::string("VM Op '") + opCodeName(activeIns->op) +
                    "' (PC " + std::to_string(activeIns->address) + ")",
                activeIns->span);
        }
        throw;
    }

    return stack.empty() ? 0.0 : stack.back();
}

}  // namespace

BytecodeProgram BytecodeCompiler::compile(const ASTNode& root) {
    BytecodeProgram prog;
    emitAst(root, prog.instructions);
    BytecodeInstruction halt;
    halt.address = prog.instructions.size();
    halt.op = OpCode::HALT;
    halt.span = root.span();
    prog.instructions.push_back(std::move(halt));
    return prog;
}

double BytecodeVM::execute(const BytecodeProgram& program,
                           SymbolTable& symbols,
                           const std::unordered_map<std::string, double>* locals,
                           std::size_t callDepth) const {
    return runVmCore(program, &symbols, symbols, locals, callDepth);
}

double BytecodeVM::executeWithSingleParam(const BytecodeProgram& program,
                                          const SymbolTable& symbols,
                                          std::string_view paramName,
                                          double paramValue) const {
    std::unordered_map<std::string, double> locals;
    locals.emplace(std::string(paramName), paramValue);
    return runVmCore(program, nullptr, symbols, &locals, 0);
}

}  // namespace mathcompiler
