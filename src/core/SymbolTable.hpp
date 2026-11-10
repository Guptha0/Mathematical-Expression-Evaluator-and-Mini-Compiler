#pragma once

#include "AST.hpp"

#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mathcompiler {

struct SymbolEntry {
    std::string name;
    double value = 0.0;
    bool isBuiltIn = false;
    bool isOverridden = false;
    std::string description;
};

struct UserFunction {
    std::string name;
    std::vector<std::string> params;
    std::string bodyExpr;
    std::shared_ptr<const ASTNode> bodyAst;
};

/// Thread-safe dynamic symbol table storing runtime variables, mathematical constants (`pi`, `e`, `phi`),
/// and user-defined parameterized functions (`f(x) = x^2 + 2*x + 1`).
class SymbolTable {
public:
    SymbolTable();
    SymbolTable(const SymbolTable& other);
    SymbolTable& operator=(const SymbolTable& other);
    SymbolTable(SymbolTable&& other) noexcept;
    SymbolTable& operator=(SymbolTable&& other) noexcept;
    ~SymbolTable() = default;

    /// Resets the symbol table to only built-in mathematical constants (`pi`, `e`, `phi`) and clears user functions.
    void resetToDefaults();

    /// Sets or updates a variable value. Returns false if `name` is invalid or reserved.
    bool set(const std::string& name, double value, std::string description = {});

    /// Removes a variable by name. Returns false if not found.
    bool remove(const std::string& name);

    [[nodiscard]] bool contains(std::string_view name) const;
    [[nodiscard]] std::optional<double> get(std::string_view name) const;
    [[nodiscard]] std::optional<SymbolEntry> getEntry(std::string_view name) const;
    [[nodiscard]] std::vector<SymbolEntry> entries() const;
    [[nodiscard]] std::size_t size() const;

    /// Registers or updates a user-defined function. Returns false if `name` is invalid or built-in.
    bool setFunction(const std::string& name,
                     std::vector<std::string> params,
                     std::shared_ptr<const ASTNode> bodyAst);

    bool removeFunction(const std::string& name);
    [[nodiscard]] bool hasFunction(std::string_view name) const;
    [[nodiscard]] std::optional<UserFunction> getFunction(std::string_view name) const;
    [[nodiscard]] std::vector<UserFunction> functions() const;

    /// Validates identifier syntax (`[A-Za-z_][A-Za-z0-9_]*`) and rejects reserved function names.
    [[nodiscard]] static bool isAssignableIdentifier(std::string_view name) noexcept;
    [[nodiscard]] static bool isValidIdentifierSyntax(std::string_view name) noexcept;

private:
    void seedBuiltInsLocked();

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SymbolEntry> table_;
    std::unordered_map<std::string, UserFunction> functions_;
};

}  // namespace mathcompiler
