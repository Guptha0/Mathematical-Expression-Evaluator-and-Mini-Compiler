#include "Plotter.hpp"

#include "CompilerException.hpp"
#include "Lexer.hpp"
#include "Parser.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace mathcompiler {

namespace {

std::unique_ptr<ASTNode> extractPlottableAst(std::string_view exprText, const SymbolTable& symbols) {
    Lexer lexer;
    Parser parser;
    const auto tokens = lexer.tokenize(exprText);
    const auto stmts = parser.parseProgram(tokens);
    if (stmts.empty() || !stmts.back().ast) {
        throw SyntaxException("E_EMPTY_EXPR", "Plot expression cannot be empty", SourceSpan{1, 1, 0, 1});
    }

    const ASTNode* root = stmts.back().ast.get();
    if (root->kind() == ASTNodeKind::Assign) {
        root = &static_cast<const AssignNode&>(*root).rhs();
    } else if (root->kind() == ASTNodeKind::FunctionDef) {
        const auto& fd = static_cast<const FunctionDefNode&>(*root);
        if (fd.params().size() == 1 && fd.params()[0] == "x") {
            root = &fd.body();
        } else {
            throw SemanticException(
                "E_PLOT_EXPR",
                "Function definition must take single parameter 'x' for 2D plotting",
                fd.span());
        }
    } else if (root->kind() == ASTNodeKind::Variable) {
        const auto& varName = static_cast<const VariableNode&>(*root).name();
        if (auto fn = symbols.getFunction(varName)) {
            if (fn->params.size() == 1) {
                std::vector<std::unique_ptr<ASTNode>> args;
                args.push_back(std::make_unique<VariableNode>("x", root->span()));
                auto callNode = std::make_unique<FunctionCallNode>(varName, std::move(args), root->span());
                auto opt = ASTOptimizer::optimize(*callNode);
                return std::move(opt.optimizedRoot);
            }
        }
    }

    auto opt = ASTOptimizer::optimize(*root);
    return std::move(opt.optimizedRoot);
}

std::vector<PlotPoint> sampleBytecodeCurve(const BytecodeProgram& prog,
                                           const SymbolTable& symbols,
                                           double xMin,
                                           double xMax,
                                           std::size_t sampleCount) {
    BytecodeVM vm;
    std::vector<PlotPoint> pts;
    pts.reserve(sampleCount);

    const double span = xMax - xMin;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const double t = (sampleCount > 1) ? static_cast<double>(i) / static_cast<double>(sampleCount - 1) : 0.0;
        const double x = xMin + t * span;
        PlotPoint pt{x, 0.0, false};
        try {
            const double y = vm.executeWithSingleParam(prog, symbols, "x", x);
            if (std::isfinite(y)) {
                pt.y = y;
                pt.valid = true;
            }
        } catch (const CompilerException&) {
            pt.valid = false;
        }
        pts.push_back(pt);
    }
    return pts;
}

}  // namespace

PlotSeries Plotter::sampleExpression(std::string_view exprText,
                                     const SymbolTable& symbols,
                                     double xMin,
                                     double xMax,
                                     std::size_t sampleCount,
                                     bool includeDerivative) const {
    PlotSeries series;
    if (xMin >= xMax) {
        series.errorMessage = "xMin must be strictly less than xMax.";
        return series;
    }

    const std::size_t clampedSamples = std::clamp<std::size_t>(sampleCount, 32, 4000);
    const auto tStart = std::chrono::steady_clock::now();

    try {
        auto ast = extractPlottableAst(exprText, symbols);
        series.expressionLabel = ast->toInfixString();
        const BytecodeProgram prog = BytecodeCompiler::compile(*ast);

        // Quick probe at midpoint to surface Semantic errors (undeclared non-x variables, unknown functions)
        {
            BytecodeVM vm;
            try {
                (void)vm.executeWithSingleParam(prog, symbols, "x", 0.5 * (xMin + xMax));
            } catch (const SemanticException& ex) {
                series.errorMessage = ex.detailMessage();
                return series;
            } catch (const CompilerException&) {
                // Runtime domain errors at midpoint (e.g. 1/x at x=0) are fine across range
            }
        }

        series.points = sampleBytecodeCurve(prog, symbols, xMin, xMax, clampedSamples);

        if (includeDerivative) {
            try {
                auto derivAst = SymbolicDiff::differentiate(*ast, "x");
                series.derivativeLabel = derivAst->toInfixString();
                const BytecodeProgram derivProg = BytecodeCompiler::compile(*derivAst);
                series.derivativePoints = sampleBytecodeCurve(derivProg, symbols, xMin, xMax, clampedSamples);
            } catch (const CompilerException&) {
                series.derivativeLabel.clear();
            }
        }

        bool foundAny = false;
        double minY = 0.0;
        double maxY = 0.0;
        auto scanBounds = [&](const std::vector<PlotPoint>& pts) {
            for (const auto& pt : pts) {
                if (!pt.valid) continue;
                if (!foundAny) {
                    minY = maxY = pt.y;
                    foundAny = true;
                } else {
                    minY = std::min(minY, pt.y);
                    maxY = std::max(maxY, pt.y);
                }
            }
        };

        scanBounds(series.points);
        if (includeDerivative) {
            scanBounds(series.derivativePoints);
        }

        if (!foundAny) {
            series.errorMessage = "Expression produced no real-valued points on [" +
                                  formatCompactNumber(xMin) + ", " + formatCompactNumber(xMax) + "].";
            return series;
        }

        if (std::fabs(maxY - minY) < 1e-9) {
            minY -= 1.0;
            maxY += 1.0;
        }
        series.minY = minY;
        series.maxY = maxY;
        series.valid = true;
    } catch (const CompilerException& ex) {
        series.errorMessage = ex.detailMessage();
    }

    const auto tEnd = std::chrono::steady_clock::now();
    series.sampleTimeUs = std::chrono::duration<double, std::micro>(tEnd - tStart).count();
    return series;
}

std::optional<double> Plotter::evaluateAt(std::string_view exprText,
                                          const SymbolTable& symbols,
                                          double xValue) const {
    try {
        auto ast = extractPlottableAst(exprText, symbols);
        const BytecodeProgram prog = BytecodeCompiler::compile(*ast);
        BytecodeVM vm;
        const double val = vm.executeWithSingleParam(prog, symbols, "x", xValue);
        if (std::isfinite(val)) {
            return val;
        }
    } catch (const CompilerException&) {
    }
    return std::nullopt;
}

}  // namespace mathcompiler
