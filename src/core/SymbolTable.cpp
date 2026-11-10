#include "SymbolTable.hpp"

#include "FunctionRegistry.hpp"

#include <algorithm>
#include <mutex>

namespace mathcompiler {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEuler = 2.71828182845904523536;
constexpr double kPhi = 1.61803398874989484820;

bool isIdentStart(char ch) noexcept {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool isIdentContinue(char ch) noexcept {
    return isIdentStart(ch) || (ch >= '0' && ch <= '9');
}

}  // namespace

SymbolTable::SymbolTable() {
    seedBuiltInsLocked();
}

SymbolTable::SymbolTable(const SymbolTable& other) {
    std::shared_lock lock(other.mutex_);
    table_ = other.table_;
    functions_ = other.functions_;
}

SymbolTable& SymbolTable::operator=(const SymbolTable& other) {
    if (this != &other) {
        std::unique_lock lhsLock(mutex_, std::defer_lock);
        std::shared_lock rhsLock(other.mutex_, std::defer_lock);
        std::lock(lhsLock, rhsLock);
        table_ = other.table_;
        functions_ = other.functions_;
    }
    return *this;
}

SymbolTable::SymbolTable(SymbolTable&& other) noexcept {
    std::unique_lock lock(other.mutex_);
    table_ = std::move(other.table_);
    functions_ = std::move(other.functions_);
}

SymbolTable& SymbolTable::operator=(SymbolTable&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mutex_, other.mutex_);
        table_ = std::move(other.table_);
        functions_ = std::move(other.functions_);
    }
    return *this;
}

void SymbolTable::seedBuiltInsLocked() {
    table_["pi"]  = SymbolEntry{"pi", kPi, true, false, "Archimedes' constant"};
    table_["e"]   = SymbolEntry{"e", kEuler, true, false, "Euler's number"};
    table_["phi"] = SymbolEntry{"phi", kPhi, true, false, "Golden ratio"};
}

void SymbolTable::resetToDefaults() {
    std::unique_lock lock(mutex_);
    table_.clear();
    functions_.clear();
    seedBuiltInsLocked();
}

bool SymbolTable::isValidIdentifierSyntax(std::string_view name) noexcept {
    if (name.empty() || !isIdentStart(name.front())) {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!isIdentContinue(name[i])) {
            return false;
        }
    }
    return true;
}

bool SymbolTable::isAssignableIdentifier(std::string_view name) noexcept {
    return isValidIdentifierSyntax(name) && !FunctionRegistry::isBuiltIn(name);
}

bool SymbolTable::set(const std::string& name, double value, std::string description) {
    if (!isAssignableIdentifier(name)) {
        return false;
    }

    std::unique_lock lock(mutex_);
    auto it = table_.find(name);
    if (it != table_.end()) {
        it->second.value = value;
        if (it->second.isBuiltIn) {
            it->second.isOverridden = true;
        }
        if (!description.empty()) {
            it->second.description = std::move(description);
        }
        return true;
    }

    table_[name] = SymbolEntry{
        name,
        value,
        false,
        false,
        description.empty() ? "User variable" : std::move(description)};
    return true;
}

bool SymbolTable::remove(const std::string& name) {
    std::unique_lock lock(mutex_);
    return table_.erase(name) > 0;
}

bool SymbolTable::contains(std::string_view name) const {
    std::shared_lock lock(mutex_);
    return table_.find(std::string(name)) != table_.end();
}

std::optional<double> SymbolTable::get(std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = table_.find(std::string(name));
    if (it == table_.end()) {
        return std::nullopt;
    }
    return it->second.value;
}

std::optional<SymbolEntry> SymbolTable::getEntry(std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = table_.find(std::string(name));
    if (it == table_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<SymbolEntry> SymbolTable::entries() const {
    std::shared_lock lock(mutex_);
    std::vector<SymbolEntry> result;
    result.reserve(table_.size());
    for (const auto& [key, entry] : table_) {
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end(), [](const SymbolEntry& a, const SymbolEntry& b) {
        if (a.isBuiltIn != b.isBuiltIn) {
            return a.isBuiltIn > b.isBuiltIn;
        }
        return a.name < b.name;
    });
    return result;
}

std::size_t SymbolTable::size() const {
    std::shared_lock lock(mutex_);
    return table_.size();
}

bool SymbolTable::setFunction(const std::string& name,
                              std::vector<std::string> params,
                              std::shared_ptr<const ASTNode> bodyAst) {
    if (!isAssignableIdentifier(name) || !bodyAst) {
        return false;
    }
    UserFunction fn{name, std::move(params), bodyAst->toInfixString(), std::move(bodyAst)};
    std::unique_lock lock(mutex_);
    functions_[name] = std::move(fn);
    return true;
}

bool SymbolTable::removeFunction(const std::string& name) {
    std::unique_lock lock(mutex_);
    return functions_.erase(name) > 0;
}

bool SymbolTable::hasFunction(std::string_view name) const {
    std::shared_lock lock(mutex_);
    return functions_.find(std::string(name)) != functions_.end();
}

std::optional<UserFunction> SymbolTable::getFunction(std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = functions_.find(std::string(name));
    if (it == functions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<UserFunction> SymbolTable::functions() const {
    std::shared_lock lock(mutex_);
    std::vector<UserFunction> out;
    out.reserve(functions_.size());
    for (const auto& [k, v] : functions_) {
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const UserFunction& a, const UserFunction& b) {
        return a.name < b.name;
    });
    return out;
}

}  // namespace mathcompiler
