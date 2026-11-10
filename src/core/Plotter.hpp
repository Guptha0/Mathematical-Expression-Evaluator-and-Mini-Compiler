#pragma once

#include "AST.hpp"
#include "BytecodeVM.hpp"
#include "SymbolTable.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mathcompiler {

struct PlotPoint {
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
};

struct PlotSeries {
    bool valid = false;
    std::string expressionLabel;
    std::string derivativeLabel;
    std::string errorMessage;
    std::vector<PlotPoint> points;
    std::vector<PlotPoint> derivativePoints;
    double minY = -1.0;
    double maxY = 1.0;
    double sampleTimeUs = 0.0;
};

/// Compiles mathematical expressions into Stack VM bytecode and samples 2D Cartesian curves.
class Plotter {
public:
    [[nodiscard]] PlotSeries sampleExpression(std::string_view exprText,
                                              const SymbolTable& symbols,
                                              double xMin,
                                              double xMax,
                                              std::size_t sampleCount = 600,
                                              bool includeDerivative = false) const;

    /// Evaluates `exprText` at a single `x` value using compiled bytecode (for crosshair readouts).
    [[nodiscard]] std::optional<double> evaluateAt(std::string_view exprText,
                                                   const SymbolTable& symbols,
                                                   double xValue) const;
};

}  // namespace mathcompiler
