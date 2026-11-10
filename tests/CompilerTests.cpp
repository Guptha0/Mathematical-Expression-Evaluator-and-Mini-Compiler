#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "AST.hpp"
#include "BytecodeVM.hpp"
#include "CompilerEngine.hpp"
#include "CompilerException.hpp"
#include "Lexer.hpp"
#include "Plotter.hpp"
#include "SymbolTable.hpp"

#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace mathcompiler;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

std::vector<std::string> rpnLexemes(const StatementArtifact& stmt) {
    std::vector<std::string> out;
    out.reserve(stmt.rpnTokens.size());
    for (const auto& t : stmt.rpnTokens) {
        out.push_back(t.displayLexeme());
    }
    return out;
}

}  // namespace

TEST_CASE("Lexer: decimal, scientific, hex, binary, and exact start/end positions", "[lexer]") {
    Lexer lexer;

    SECTION("Tokenizes hex (0x), binary (0b), scientific notation, and multi-char identifiers") {
        auto tokens = lexer.tokenize("val_1 = 0xFF + 0b1010 + 1.5e-2");
        REQUIRE(tokens.size() == 8);  // val_1, =, 0xFF, +, 0b1010, +, 1.5e-2, EOF

        REQUIRE(tokens[0].type == TokenType::Identifier);
        REQUIRE(tokens[0].lexeme == "val_1");
        REQUIRE(tokens[0].span.column == 1);
        REQUIRE(tokens[0].span.endColumn() == 5);

        REQUIRE(tokens[2].type == TokenType::Number);
        REQUIRE(tokens[2].lexeme == "0xFF");
        REQUIRE_THAT(tokens[2].numericValue, WithinAbs(255.0, 1e-12));
        REQUIRE(tokens[2].span.column == 9);
        REQUIRE(tokens[2].span.endColumn() == 12);

        REQUIRE(tokens[4].type == TokenType::Number);
        REQUIRE_THAT(tokens[4].numericValue, WithinAbs(10.0, 1e-12));

        REQUIRE(tokens[6].type == TokenType::Number);
        REQUIRE_THAT(tokens[6].numericValue, WithinAbs(0.015, 1e-12));
    }

    SECTION("Tracks line and column indices across multiple lines") {
        auto tokens = lexer.tokenize("a = 10\n  b = a + 20");
        REQUIRE(tokens[4].lexeme == "b");
        REQUIRE(tokens[4].span.line == 2);
        REQUIRE(tokens[4].span.column == 3);
        REQUIRE(tokens[8].lexeme == "20");
        REQUIRE(tokens[8].span.line == 2);
        REQUIRE(tokens[8].span.column == 11);
        REQUIRE(tokens[8].span.endColumn() == 12);
    }

    SECTION("Rejects malformed hex/binary literals, extra decimal points, and bad chars") {
        REQUIRE_THROWS_AS(lexer.tokenize("0x"), LexicalException);
        REQUIRE_THROWS_AS(lexer.tokenize("0x1G"), LexicalException);
        REQUIRE_THROWS_AS(lexer.tokenize("0b102"), LexicalException);
        REQUIRE_THROWS_AS(lexer.tokenize("5 + @ 2"), LexicalException);
        REQUIRE_THROWS_AS(lexer.tokenize("3.14.15"), LexicalException);
        REQUIRE_THROWS_AS(lexer.tokenize("1.5e+"), LexicalException);
    }

    SECTION("Highlighting mode preserves malformed tokens as TokenType::Invalid") {
        auto hl = lexer.tokenizeForHighlighting("5 + @ 0b102");
        REQUIRE(hl.size() >= 5);
        REQUIRE(hl[2].type == TokenType::Invalid);
        REQUIRE(hl[2].lexeme == "@");
        REQUIRE(hl[3].type == TokenType::Invalid);
        REQUIRE(hl[3].lexeme == "0b102");
    }
}

TEST_CASE("Shunting-Yard Parser & Bytecode VM: precedence, associativity, and VM parity", "[parser][vm]") {
    CompilerEngine engine;

    SECTION("Operator precedence, RPN, and Bytecode disassembly") {
        auto r1 = engine.compileAndEvaluate("2 + 3 * 4");
        REQUIRE(r1.success);
        REQUIRE(r1.hasNumericValue);
        REQUIRE_THAT(r1.finalValue, WithinAbs(14.0, 1e-12));
        REQUIRE_THAT(r1.vmFinalValue, WithinAbs(14.0, 1e-12));
        REQUIRE(rpnLexemes(r1.statements[0]) == std::vector<std::string>{"2", "3", "4", "*", "+"});

        const std::string asmText = r1.statements[0].bytecode.disassemble();
        REQUIRE_THAT(asmText, ContainsSubstring("PUSH"));
        REQUIRE_THAT(asmText, ContainsSubstring("HALT"));
    }

    SECTION("Right-associative power vs left-associative subtraction and modulo") {
        REQUIRE_THAT(engine.compileAndEvaluate("2 ^ 3 ^ 2").vmFinalValue, WithinAbs(512.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("10 - 3 - 2").vmFinalValue, WithinAbs(5.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("17 % 5").vmFinalValue, WithinAbs(2.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("((2 + 3) * (4 + 6)) / (2 ^ 2 + 1)").vmFinalValue,
                     WithinAbs(10.0, 1e-12));
    }

    SECTION("Unary minus and unary plus precedence interactions") {
        REQUIRE_THAT(engine.compileAndEvaluate("-5").vmFinalValue, WithinAbs(-5.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("--5").vmFinalValue, WithinAbs(5.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("-2^2").vmFinalValue, WithinAbs(-4.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("(-2)^2").vmFinalValue, WithinAbs(4.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("2^-3").vmFinalValue, WithinAbs(0.125, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("+7 + -2").vmFinalValue, WithinAbs(5.0, 1e-12));
    }
}

TEST_CASE("User-defined functions, constants, and math library", "[functions][vm]") {
    CompilerEngine engine;

    SECTION("Supports pi, e, phi and <cmath> functions on both VM and AST-Walker") {
        const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
        REQUIRE_THAT(engine.compileAndEvaluate("phi").vmFinalValue, WithinAbs(phi, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("sin(pi / 2)").vmFinalValue, WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("cos(0)").vmFinalValue, WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("tan(pi / 4)").vmFinalValue, WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("sqrt(81)").vmFinalValue, WithinAbs(9.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("ln(e^3)").vmFinalValue, WithinAbs(3.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("log10(1000)").vmFinalValue, WithinAbs(3.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("exp(2)").vmFinalValue, WithinAbs(std::exp(2.0), 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("pow(2, 10)").vmFinalValue, WithinAbs(1024.0, 1e-12));
        REQUIRE_THAT(engine.compileAndEvaluate("abs(-42.5)").vmFinalValue, WithinAbs(42.5, 1e-12));
    }

    SECTION("Defines and executes single- and multi-parameter user functions") {
        auto r = engine.compileAndEvaluate("f(x) = x^2 + 2*x + 1\nf(4)");
        REQUIRE(r.success);
        REQUIRE(r.hasNumericValue);
        REQUIRE_THAT(r.finalValue, WithinAbs(25.0, 1e-12));
        REQUIRE_THAT(r.vmFinalValue, WithinAbs(25.0, 1e-12));

        auto r2 = engine.compileAndEvaluate("hypot(a, b) = sqrt(a^2 + b^2)\nhypot(3, 4)");
        REQUIRE(r2.success);
        REQUIRE_THAT(r2.vmFinalValue, WithinAbs(5.0, 1e-12));

        // Guards against infinite recursion in cyclic user functions
        auto cyc = engine.compileAndEvaluate("loop(x) = loop(x)\nloop(1)");
        REQUIRE_FALSE(cyc.success);
        REQUIRE(cyc.error->errorCode == "E_STACK_OVERFLOW");
    }
}

TEST_CASE("Symbolic and numerical differentiation", "[symbolic][diff]") {
    CompilerEngine engine;

    SECTION("Computes symbolic derivatives without requiring numeric binding") {
        auto d1 = engine.compileAndEvaluate("diff(x^2, x)");
        REQUIRE(d1.success);
        REQUIRE_FALSE(d1.hasNumericValue);
        REQUIRE(d1.symbolicResult == "2 * x");

        auto d2 = engine.compileAndEvaluate("diff(x^2 + 2*x + 1, x)");
        REQUIRE(d2.success);
        REQUIRE(d2.symbolicResult == "2 * x + 2");

        auto d3 = engine.compileAndEvaluate("diff(diff(x^3, x), x)");
        REQUIRE(d3.success);
        REQUIRE(d3.symbolicResult == "6 * x");
    }

    SECTION("Evaluates symbolic and numerical derivatives accurately at a point") {
        auto res = engine.compileAndEvaluate("x = 2\ndiff(x^3 + 3*x^2 - 5*x + 2, x)");
        REQUIRE(res.success);
        REQUIRE(res.hasNumericValue);
        // 3*x^2 + 6*x - 5 at x=2 -> 12 + 12 - 5 = 19
        REQUIRE_THAT(res.vmFinalValue, WithinAbs(19.0, 1e-12));

        auto chain = engine.compileAndEvaluate("x = 0\ndiff(sin(2 * x), x)");
        REQUIRE(chain.success);
        REQUIRE_THAT(chain.vmFinalValue, WithinAbs(2.0, 1e-12));

        auto num1 = engine.compileAndEvaluate("ndiff(sin(x), x, 0)");
        REQUIRE(num1.success);
        REQUIRE_THAT(num1.vmFinalValue, WithinAbs(1.0, 1e-6));

        auto num2 = engine.compileAndEvaluate("ndiff(x^3, x, 2)");
        REQUIRE(num2.success);
        REQUIRE_THAT(num2.vmFinalValue, WithinAbs(12.0, 1e-5));
    }
}

TEST_CASE("AST Optimization: constant folding, dead-code elimination, and error preservation", "[ast][optimizer]") {
    CompilerEngine engine;

    SECTION("Folds constants and eliminates dead statements & overwritten stores") {
        auto res = engine.compileAndEvaluate("2 + 3 * 4\na = 5\na = 10\na * (1 + 2)", true);
        REQUIRE(res.success);
        REQUIRE_THAT(res.vmFinalValue, WithinAbs(30.0, 1e-12));
        REQUIRE(res.statements[0].eliminatedAsDeadCode);
        REQUIRE(res.statements[1].eliminatedAsDeadCode);
        REQUIRE_FALSE(res.statements[2].eliminatedAsDeadCode);
        REQUIRE(res.deadCodeLog.size() == 2);
    }

    SECTION("Preserves semantic and domain errors during optimization") {
        auto undecl = engine.compileAndEvaluate("missingVar + 0", true);
        REQUIRE_FALSE(undecl.success);
        REQUIRE(undecl.error->errorCode == "E_UNDECLARED_VAR");

        auto domErr = engine.compileAndEvaluate("1 + sqrt(-1)", true);
        REQUIRE_FALSE(domErr.success);
        REQUIRE(domErr.error->errorCode == "E_MATH_DOMAIN");
    }
}

TEST_CASE("2D Function Plotter: sampling, discontinuities, and derivative overlay", "[plotter]") {
    SymbolTable symbols;
    Plotter plotter;

    auto s1 = plotter.sampleExpression("y = sin(x)", symbols, -3.14159, 3.14159, 200, true);
    REQUIRE(s1.valid);
    REQUIRE(s1.points.size() == 200);
    REQUIRE(s1.derivativePoints.size() == 200);
    REQUIRE_THAT(s1.minY, WithinAbs(-1.0, 0.02));
    REQUIRE_THAT(s1.maxY, WithinAbs(1.0, 0.02));

    auto sSqrt = plotter.sampleExpression("sqrt(x)", symbols, -4.0, 4.0, 100, false);
    REQUIRE(sSqrt.valid);
    REQUIRE_FALSE(sSqrt.points.front().valid);  // x = -4 is outside sqrt domain
    REQUIRE(sSqrt.points.back().valid);         // x = +4 -> 2.0
    REQUIRE_THAT(sSqrt.points.back().y, WithinAbs(2.0, 1e-9));
}

TEST_CASE("Symbol Table concurrency, error diagnostics, and result formatting", "[diagnostics][symboltable]") {
    SECTION("Thread-safe SymbolTable handles concurrent readers and writers") {
        SymbolTable table;
        table.set("shared_v", 1.0);

        std::vector<std::thread> workers;
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&table, t]() {
                for (int i = 0; i < 200; ++i) {
                    table.set("var_" + std::to_string(t), static_cast<double>(i));
                    (void)table.get("shared_v");
                    (void)table.entries();
                }
            });
        }
        for (auto& w : workers) {
            w.join();
        }
        REQUIRE(table.contains("shared_v"));
    }

    SECTION("Syntax, semantic, and runtime error reporting with caret pointers and unwind stacks") {
        CompilerEngine engine;

        auto unbal = engine.compileAndEvaluate("5 + (3 * 2");
        REQUIRE_FALSE(unbal.success);
        REQUIRE(unbal.error->errorCode == "E_UNBALANCED_PARENS");
        REQUIRE(unbal.error->span.column == 5);
        REQUIRE_THAT(unbal.error->caretBlock, ContainsSubstring("    ^"));

        auto consec = engine.compileAndEvaluate("5 + * 2");
        REQUIRE_FALSE(consec.success);
        REQUIRE(consec.error->errorCode == "E_UNEXPECTED_OPERATOR");
        REQUIRE(consec.error->span.column == 5);

        REQUIRE(engine.compileAndEvaluate("pow(2)").error->errorCode == "E_ARITY_MISMATCH");
        REQUIRE(engine.compileAndEvaluate("sin()").error->errorCode == "E_ARITY_MISMATCH");
        REQUIRE(engine.compileAndEvaluate("foobar(10)").error->errorCode == "E_UNKNOWN_FUNC");
        REQUIRE(engine.compileAndEvaluate("ln(-5)").error->errorCode == "E_MATH_DOMAIN");

        auto divZero = engine.compileAndEvaluate("10 + (8 / (4 - 4))");
        REQUIRE_FALSE(divZero.success);
        REQUIRE(divZero.error->errorCode == "E_DIV_ZERO");
        REQUIRE_FALSE(divZero.error->callStack.empty());
    }

    SECTION("Formatting modes: Auto, Fixed, Scientific, and Fractional") {
        REQUIRE(CompilerEngine::formatResult(3.14159265, NumberFormatMode::Fixed, 2) == "3.14");
        REQUIRE_THAT(CompilerEngine::formatResult(1234.6, NumberFormatMode::Scientific, 3),
                     ContainsSubstring("1.235e+03"));
        REQUIRE(CompilerEngine::formatResult(0.5, NumberFormatMode::Fraction, 6) == "1/2");
        REQUIRE(CompilerEngine::formatResult(1.0 / 3.0, NumberFormatMode::Fraction, 6) == "1/3");
        REQUIRE(CompilerEngine::formatResult(-2.75, NumberFormatMode::Fraction, 6) == "-11/4");
        REQUIRE(CompilerEngine::formatResult(3.14159265358979, NumberFormatMode::Fraction, 4) == "~355/113");
    }
}
