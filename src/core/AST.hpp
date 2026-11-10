#pragma once

#include "MathOps.hpp"
#include "SourceLocation.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mathcompiler {

enum class ASTNodeKind {
    Number,
    Variable,
    UnaryOp,
    BinaryOp,
    FunctionCall,
    Assign,
    FunctionDef
};

/// Abstract base class for all Abstract Syntax Tree nodes.
class ASTNode {
public:
    explicit ASTNode(SourceSpan span) : span_(span) {}
    virtual ~ASTNode() = default;

    ASTNode(const ASTNode&) = delete;
    ASTNode& operator=(const ASTNode&) = delete;

    [[nodiscard]] const SourceSpan& span() const noexcept { return span_; }
    [[nodiscard]] virtual ASTNodeKind kind() const noexcept = 0;
    [[nodiscard]] virtual const char* kindName() const noexcept = 0;
    [[nodiscard]] virtual std::string label() const = 0;
    [[nodiscard]] virtual std::vector<const ASTNode*> childNodes() const = 0;
    [[nodiscard]] virtual std::unique_ptr<ASTNode> clone() const = 0;

    [[nodiscard]] std::size_t nodeCount() const;
    [[nodiscard]] std::size_t depth() const;
    [[nodiscard]] virtual bool isFoldedConstant() const noexcept { return false; }

    /// Formats an indented tree representation of this subtree.
    [[nodiscard]] std::string toTreeString(int indent = 0) const;

    /// Renders this subtree back to clean infix mathematical notation.
    [[nodiscard]] std::string toInfixString(int parentPrecedence = -1, bool isRightChild = false) const;

protected:
    SourceSpan span_;
};

class NumberNode final : public ASTNode {
public:
    NumberNode(double value, SourceSpan span, bool folded = false)
        : ASTNode(span), value_(value), folded_(folded) {}

    [[nodiscard]] double value() const noexcept { return value_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::Number; }
    [[nodiscard]] const char* kindName() const noexcept override { return "Number"; }
    [[nodiscard]] std::string label() const override { return formatCompactNumber(value_); }
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override { return {}; }
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;
    [[nodiscard]] bool isFoldedConstant() const noexcept override { return folded_; }

private:
    double value_;
    bool folded_;
};

class VariableNode final : public ASTNode {
public:
    VariableNode(std::string name, SourceSpan span)
        : ASTNode(span), name_(std::move(name)) {}

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::Variable; }
    [[nodiscard]] const char* kindName() const noexcept override { return "Variable"; }
    [[nodiscard]] std::string label() const override { return name_; }
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override { return {}; }
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    std::string name_;
};

class UnaryOpNode final : public ASTNode {
public:
    UnaryOpNode(UnaryOpKind op, std::unique_ptr<ASTNode> operand, SourceSpan span)
        : ASTNode(span), op_(op), operand_(std::move(operand)) {}

    [[nodiscard]] UnaryOpKind op() const noexcept { return op_; }
    [[nodiscard]] const ASTNode& operand() const noexcept { return *operand_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::UnaryOp; }
    [[nodiscard]] const char* kindName() const noexcept override { return "UnaryOp"; }
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override;
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    UnaryOpKind op_;
    std::unique_ptr<ASTNode> operand_;
};

class BinaryOpNode final : public ASTNode {
public:
    BinaryOpNode(BinaryOpKind op,
                 std::unique_ptr<ASTNode> left,
                 std::unique_ptr<ASTNode> right,
                 SourceSpan span)
        : ASTNode(span), op_(op), left_(std::move(left)), right_(std::move(right)) {}

    [[nodiscard]] BinaryOpKind op() const noexcept { return op_; }
    [[nodiscard]] const ASTNode& left() const noexcept { return *left_; }
    [[nodiscard]] const ASTNode& right() const noexcept { return *right_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::BinaryOp; }
    [[nodiscard]] const char* kindName() const noexcept override { return "BinaryOp"; }
    [[nodiscard]] std::string label() const override { return binaryOpSymbol(op_); }
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override;
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    BinaryOpKind op_;
    std::unique_ptr<ASTNode> left_;
    std::unique_ptr<ASTNode> right_;
};

class FunctionCallNode final : public ASTNode {
public:
    FunctionCallNode(std::string funcName,
                     std::vector<std::unique_ptr<ASTNode>> args,
                     SourceSpan span)
        : ASTNode(span), funcName_(std::move(funcName)), args_(std::move(args)) {}

    [[nodiscard]] const std::string& funcName() const noexcept { return funcName_; }
    [[nodiscard]] const std::vector<std::unique_ptr<ASTNode>>& args() const noexcept { return args_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::FunctionCall; }
    [[nodiscard]] const char* kindName() const noexcept override { return "Function"; }
    [[nodiscard]] std::string label() const override { return funcName_ + "()"; }
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override;
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    std::string funcName_;
    std::vector<std::unique_ptr<ASTNode>> args_;
};

class AssignNode final : public ASTNode {
public:
    AssignNode(std::string targetName,
               SourceSpan targetSpan,
               std::unique_ptr<ASTNode> rhs,
               SourceSpan span)
        : ASTNode(span),
          targetName_(std::move(targetName)),
          targetSpan_(targetSpan),
          rhs_(std::move(rhs)) {}

    [[nodiscard]] const std::string& targetName() const noexcept { return targetName_; }
    [[nodiscard]] const SourceSpan& targetSpan() const noexcept { return targetSpan_; }
    [[nodiscard]] const ASTNode& rhs() const noexcept { return *rhs_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::Assign; }
    [[nodiscard]] const char* kindName() const noexcept override { return "Assign"; }
    [[nodiscard]] std::string label() const override { return targetName_ + " ="; }
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override;
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    std::string targetName_;
    SourceSpan targetSpan_;
    std::unique_ptr<ASTNode> rhs_;
};

/// User-defined parameterized function definition, e.g. `f(x) = x^2 + 2*x + 1`.
class FunctionDefNode final : public ASTNode {
public:
    FunctionDefNode(std::string funcName,
                    SourceSpan funcSpan,
                    std::vector<std::string> params,
                    std::unique_ptr<ASTNode> body,
                    SourceSpan span)
        : ASTNode(span),
          funcName_(std::move(funcName)),
          funcSpan_(funcSpan),
          params_(std::move(params)),
          body_(std::move(body)) {}

    [[nodiscard]] const std::string& funcName() const noexcept { return funcName_; }
    [[nodiscard]] const SourceSpan& funcSpan() const noexcept { return funcSpan_; }
    [[nodiscard]] const std::vector<std::string>& params() const noexcept { return params_; }
    [[nodiscard]] const ASTNode& body() const noexcept { return *body_; }
    [[nodiscard]] ASTNodeKind kind() const noexcept override { return ASTNodeKind::FunctionDef; }
    [[nodiscard]] const char* kindName() const noexcept override { return "FuncDef"; }
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] std::vector<const ASTNode*> childNodes() const override;
    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override;

private:
    std::string funcName_;
    SourceSpan funcSpan_;
    std::vector<std::string> params_;
    std::unique_ptr<ASTNode> body_;
};

struct OptimizationResult {
    std::unique_ptr<ASTNode> optimizedRoot;
    std::size_t rawNodeCount = 0;
    std::size_t optimizedNodeCount = 0;
    std::size_t foldsPerformed = 0;
    std::vector<std::string> passLog;
};

/// Symbolic differentiation engine operating directly on AST subtrees.
class SymbolicDiff {
public:
    /// Computes d(`expr`)/d(`variable`) and applies algebraic calculus simplification.
    [[nodiscard]] static std::unique_ptr<ASTNode> differentiate(const ASTNode& expr,
                                                                std::string_view variable);

    /// Applies algebraic simplification (0*x->0, 1*x->x, x+0->x, x^1->x, x^0->1, constant folding).
    [[nodiscard]] static std::unique_ptr<ASTNode> simplify(const ASTNode& expr);

    /// Returns true if `node` references `varName` (free variable check for dead-code/differentiation).
    [[nodiscard]] static bool referencesVariable(const ASTNode& node, std::string_view varName);

    /// Returns true if `node` contains a symbolic `diff(...)` call.
    [[nodiscard]] static bool containsDiffCall(const ASTNode& node);
};

/// Performs constant folding, symbolic `diff()` lowering, algebraic simplification, and dead-code elimination.
class ASTOptimizer {
public:
    [[nodiscard]] static OptimizationResult optimize(const ASTNode& root);
};

}  // namespace mathcompiler
