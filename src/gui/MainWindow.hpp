#pragma once

#include "AST.hpp"
#include "CompilerEngine.hpp"
#include "Plotter.hpp"
#include "Theme.hpp"

#include <deque>
#include <string>
#include <vector>

namespace mathcompiler::gui {

struct ConsoleLogEntry {
    bool isError = false;
    std::string timestamp;
    std::string summary;
    std::string detail;
};

struct VariableEditBuffer {
    std::string name;
    char valueText[64]{};
};

/// Main desktop workspace managing editor, 2D plotter, symbol inspector, pipeline visualizers,
/// bytecode viewer, microsecond profiler, and diagnostic console.
class MainWindow {
public:
    explicit MainWindow(FontBundle fonts, float dpiScale = 1.0f);

    void render();

private:
    void evaluateExpression(bool recordHistory);
    void refreshPlot();
    void insertTextAtCursor(const std::string& text);
    void backspaceAtCursor();
    void loadPreset(const std::string& expr);
    void syncVariableBuffers();
    void appendConsoleLog(bool isError, std::string summary, std::string detail = {});

    // Workspace Panes
    void renderHeaderBar();
    void renderExpressionEditorCard();
    void renderAutocompleteBar();
    void renderSyntaxHighlightPreview();
    void renderResultAndProfilerCard();
    void renderKeypadCard();
    void renderInspectorPanel();
    void renderTokenStreamTab(const StatementArtifact& stmt);
    void renderAstTab(const StatementArtifact& stmt);
    void renderAstGraphCanvas(const ASTNode& root);
    void renderAstTree(const ASTNode& node, int depth);
    void renderBytecodeTab(const StatementArtifact& stmt);
    void renderShuntingYardTab(const StatementArtifact& stmt);
    void renderProfilerAndTraceTab(const StatementArtifact& stmt);
    void renderPlotterPanel();
    void renderVariablesDrawer();
    void renderErrorConsole();

    static int expressionInputCallback(ImGuiInputTextCallbackData* data);

    FontBundle fonts_;
    float dpiScale_ = 1.0f;
    CompilerEngine engine_;
    Lexer highlightLexer_;
    Plotter plotter_;

    // Editor state
    std::string expressionText_;
    std::string lastEvaluatedText_;
    int cursorPos_ = 0;
    int selectionStart_ = 0;
    int selectionEnd_ = 0;
    int pendingCursorPos_ = -1;
    bool requestFocusEditor_ = true;

    // Settings & panel visibility
    bool autoEvaluate_ = true;
    bool enableOptimization_ = true;
    bool showKeypad_ = false;
    bool showPlotter_ = true;
    bool showVariablesDrawer_ = true;
    bool showConsole_ = true;

    // Formatting
    NumberFormatMode formatMode_ = NumberFormatMode::Auto;
    int precision_ = 6;
    double copyToastTimer_ = 0.0;

    // Evaluation artifacts & history
    CompilationResult lastResult_;
    std::size_t selectedStmtIndex_ = 0;
    std::vector<std::string> expressionHistory_;
    std::deque<ConsoleLogEntry> consoleLogs_;

    // Shunting-Yard step playback state
    std::size_t shuntingStepIndex_ = 0;
    bool shuntingPlaying_ = false;
    float shuntingPlayTimer_ = 0.0f;

    // AST Graph view state
    int astViewMode_ = 2;  // 0: Graph, 1: Tree, 2: Split
    bool showOptimizedAst_ = true;
    float astZoom_ = 1.0f;
    ImVec2 astPan_ = ImVec2(0.0f, 0.0f);

    // 2D Function Plotter state
    char plotExprBuf_[256]{};
    double plotXMin_ = -10.0;
    double plotXMax_ = 10.0;
    double plotYMin_ = -4.0;
    double plotYMax_ = 4.0;
    bool plotAutoY_ = true;
    bool plotShowDerivative_ = true;
    bool plotSyncWithEditor_ = false;
    int plotSamples_ = 500;
    PlotSeries currentPlot_;

    // Symbol inspector state
    char newVarName_[64]{};
    char newVarValue_[64]{};
    char varFilter_[64]{};
    std::string varFeedbackError_;
    std::vector<VariableEditBuffer> varEditBuffers_;
};

}  // namespace mathcompiler::gui
