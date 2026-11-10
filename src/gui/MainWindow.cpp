#include "MainWindow.hpp"

#include "FunctionRegistry.hpp"
#include "MathOps.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace mathcompiler::gui {

namespace {

ImU32 toU32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImVec4 colorWithAlpha(ImVec4 c, float alpha) {
    c.w = alpha;
    return c;
}

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tmNow{};
#ifdef _WIN32
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
    return std::string(buf);
}

double computeNiceStep(double range, int targetLines) {
    if (range <= 0.0 || !std::isfinite(range)) return 1.0;
    const double rough = range / static_cast<double>(std::max(2, targetLines));
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    const double norm = rough / mag;
    double nice = 1.0;
    if (norm < 1.5) nice = 1.0;
    else if (norm < 3.5) nice = 2.0;
    else if (norm < 7.5) nice = 5.0;
    else nice = 10.0;
    return nice * mag;
}

ImVec4 tokenSyntaxColor(const Token& tok) {
    switch (tok.type) {
        case TokenType::Number:
            return Palette::AccentAmber;
        case TokenType::Function:
            return Palette::AccentCyan;
        case TokenType::Identifier:
            if (FunctionRegistry::isBuiltIn(tok.lexeme)) {
                return Palette::AccentCyan;
            }
            if (tok.lexeme == "pi" || tok.lexeme == "e" || tok.lexeme == "phi") {
                return Palette::AccentPurple;
            }
            return Palette::TextPrimary;
        case TokenType::Plus:
        case TokenType::Minus:
        case TokenType::Multiply:
        case TokenType::Divide:
        case TokenType::Power:
        case TokenType::Modulo:
        case TokenType::UnaryMinus:
        case TokenType::UnaryPlus:
        case TokenType::Assign:
            return Palette::AccentPink;
        case TokenType::LeftParen:
        case TokenType::RightParen:
        case TokenType::Comma:
        case TokenType::Semicolon:
            return Palette::TextSecondary;
        case TokenType::Invalid:
            return Palette::AccentRose;
        default:
            return Palette::TextMuted;
    }
}

ImVec4 astNodeColor(const ASTNode& node) {
    switch (node.kind()) {
        case ASTNodeKind::Number:
            return node.isFoldedConstant() ? Palette::AccentEmerald : Palette::AccentAmber;
        case ASTNodeKind::Variable:
            return Palette::AccentPurple;
        case ASTNodeKind::UnaryOp:
            return Palette::AccentIndigo;
        case ASTNodeKind::BinaryOp:
            return Palette::AccentPink;
        case ASTNodeKind::FunctionCall:
            return Palette::AccentCyan;
        case ASTNodeKind::Assign:
        case ASTNodeKind::FunctionDef:
            return Palette::AccentEmerald;
    }
    return Palette::TextSecondary;
}

ImVec4 opcodeColor(OpCode op) {
    switch (op) {
        case OpCode::PUSH:     return Palette::AccentAmber;
        case OpCode::LOAD:
        case OpCode::STORE:    return Palette::AccentPurple;
        case OpCode::DEF_FUNC:
        case OpCode::CALL:     return Palette::AccentCyan;
        case OpCode::ADD:
        case OpCode::SUB:
        case OpCode::MUL:
        case OpCode::DIV:
        case OpCode::POW:
        case OpCode::MOD:
        case OpCode::NEG:
        case OpCode::POS:      return Palette::AccentPink;
        case OpCode::POP:
        case OpCode::HALT:     return Palette::TextSecondary;
    }
    return Palette::TextPrimary;
}

void drawPillBadge(const char* text, const ImVec4& fg, const ImVec4& bg) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pad(8.0f, 3.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 rectMax(pos.x + textSize.x + pad.x * 2.0f, pos.y + textSize.y + pad.y * 2.0f);

    dl->AddRectFilled(pos, rectMax, toU32(bg), 10.0f);
    dl->AddRect(pos, rectMax, toU32(colorWithAlpha(fg, 0.45f)), 10.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), toU32(fg), text);
    ImGui::Dummy(ImVec2(rectMax.x - pos.x, rectMax.y - pos.y));
}

struct AstLayoutNode {
    const ASTNode* ast = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    int depth = 0;
    std::vector<int> childIndices;
};

float layoutSubtree(const ASTNode* node,
                    int depth,
                    float& nextLeafX,
                    std::vector<AstLayoutNode>& outNodes,
                    int& outIndex) {
    const int myIdx = static_cast<int>(outNodes.size());
    outNodes.push_back(AstLayoutNode{node, 0.0f, static_cast<float>(depth), depth, {}});
    outIndex = myIdx;

    const auto children = node->childNodes();
    if (children.empty()) {
        outNodes[myIdx].x = nextLeafX;
        nextLeafX += 1.0f;
        return outNodes[myIdx].x;
    }

    std::vector<int> childIdxList;
    childIdxList.reserve(children.size());
    float minChildX = 0.0f;
    float maxChildX = 0.0f;
    for (std::size_t i = 0; i < children.size(); ++i) {
        int cIdx = -1;
        const float cx = layoutSubtree(children[i], depth + 1, nextLeafX, outNodes, cIdx);
        childIdxList.push_back(cIdx);
        if (i == 0) {
            minChildX = maxChildX = cx;
        } else {
            minChildX = std::min(minChildX, cx);
            maxChildX = std::max(maxChildX, cx);
        }
    }

    outNodes[myIdx].childIndices = std::move(childIdxList);
    outNodes[myIdx].x = 0.5f * (minChildX + maxChildX);
    return outNodes[myIdx].x;
}

}  // namespace

MainWindow::MainWindow(FontBundle fonts, float dpiScale)
    : fonts_(fonts), dpiScale_(dpiScale > 0.5f ? dpiScale : 1.0f) {
    expressionText_ = "f(x) = sin(x) + 0.25 * x^2; f(pi / 2)";
    cursorPos_ = static_cast<int>(expressionText_.size());
    pendingCursorPos_ = cursorPos_;

    std::snprintf(plotExprBuf_, sizeof(plotExprBuf_), "%s", "sin(x) + 0.25 * x^2");
    std::snprintf(newVarName_, sizeof(newVarName_), "%s", "x");
    std::snprintf(newVarValue_, sizeof(newVarValue_), "%s", "2.5");

    syncVariableBuffers();
    evaluateExpression(true);
    refreshPlot();
}

int MainWindow::expressionInputCallback(ImGuiInputTextCallbackData* data) {
    auto* self = static_cast<MainWindow*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = &self->expressionText_;
        str->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = str->data();
    } else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (self->pendingCursorPos_ >= 0) {
            const int clamped = std::clamp(self->pendingCursorPos_, 0, data->BufTextLen);
            data->CursorPos = clamped;
            data->SelectionStart = clamped;
            data->SelectionEnd = clamped;
            self->pendingCursorPos_ = -1;
        }
        self->cursorPos_ = data->CursorPos;
        self->selectionStart_ = data->SelectionStart;
        self->selectionEnd_ = data->SelectionEnd;
    }
    return 0;
}

void MainWindow::evaluateExpression(bool recordHistory) {
    lastEvaluatedText_ = expressionText_;
    lastResult_ = engine_.compileAndEvaluate(expressionText_, enableOptimization_);
    selectedStmtIndex_ = lastResult_.statements.empty() ? 0 : (lastResult_.statements.size() - 1);
    shuntingStepIndex_ = 0;
    shuntingPlaying_ = false;
    syncVariableBuffers();

    if (lastResult_.success && !lastResult_.emptyInput && !lastResult_.statements.empty()) {
        const auto& lastStmt = lastResult_.statements[selectedStmtIndex_];
        shuntingStepIndex_ = lastStmt.shuntingYardSteps.empty()
                                 ? 0
                                 : lastStmt.shuntingYardSteps.size() - 1;
        if (plotSyncWithEditor_) {
            std::snprintf(plotExprBuf_, sizeof(plotExprBuf_), "%s", expressionText_.c_str());
        }
        refreshPlot();
    }

    if (recordHistory && !expressionText_.empty()) {
        if (expressionHistory_.empty() || expressionHistory_.front() != expressionText_) {
            expressionHistory_.insert(expressionHistory_.begin(), expressionText_);
            if (expressionHistory_.size() > 25) {
                expressionHistory_.pop_back();
            }
        }
        if (lastResult_.success && !lastResult_.emptyInput) {
            std::string summaryVal;
            if (!lastResult_.symbolicResult.empty() && lastResult_.hasNumericValue) {
                summaryVal = lastResult_.symbolicResult + "  (= " +
                             CompilerEngine::formatResult(lastResult_.finalValue, formatMode_, precision_) + ")";
            } else if (!lastResult_.symbolicResult.empty()) {
                summaryVal = lastResult_.symbolicResult;
            } else {
                summaryVal = CompilerEngine::formatResult(lastResult_.finalValue, formatMode_, precision_);
            }
            char timing[64]{};
            std::snprintf(timing, sizeof(timing), "Total %.1f us (VM %.1f us)",
                          lastResult_.profile.totalUs, lastResult_.profile.vmExecUs);
            appendConsoleLog(false, expressionText_ + "  =>  " + summaryVal, timing);
        } else if (lastResult_.error.has_value()) {
            appendConsoleLog(true,
                             "[" + errorPhaseToString(lastResult_.error->phase) + "] Line " +
                                 std::to_string(lastResult_.error->span.line) + ", Col " +
                                 std::to_string(lastResult_.error->span.column) + ": " +
                                 lastResult_.error->message,
                             lastResult_.error->caretBlock);
        }
    }
}

void MainWindow::refreshPlot() {
    currentPlot_ = plotter_.sampleExpression(
        plotExprBuf_,
        engine_.symbolTable(),
        plotXMin_,
        plotXMax_,
        static_cast<std::size_t>(plotSamples_),
        plotShowDerivative_);
    if (currentPlot_.valid && plotAutoY_) {
        const double pad = 0.12 * (currentPlot_.maxY - currentPlot_.minY);
        plotYMin_ = currentPlot_.minY - pad;
        plotYMax_ = currentPlot_.maxY + pad;
    }
}

void MainWindow::insertTextAtCursor(const std::string& text) {
    const int len = static_cast<int>(expressionText_.size());
    const int start = std::clamp(std::min(selectionStart_, selectionEnd_), 0, len);
    const int end = std::clamp(std::max(selectionStart_, selectionEnd_), 0, len);
    if (start == end) {
        const int pos = std::clamp(cursorPos_, 0, len);
        expressionText_.insert(static_cast<std::size_t>(pos), text);
        cursorPos_ = pos + static_cast<int>(text.size());
    } else {
        expressionText_.replace(static_cast<std::size_t>(start),
                                static_cast<std::size_t>(end - start),
                                text);
        cursorPos_ = start + static_cast<int>(text.size());
    }
    selectionStart_ = cursorPos_;
    selectionEnd_ = cursorPos_;
    pendingCursorPos_ = cursorPos_;
    requestFocusEditor_ = true;

    if (autoEvaluate_) {
        evaluateExpression(false);
    }
}

void MainWindow::backspaceAtCursor() {
    const int len = static_cast<int>(expressionText_.size());
    if (len == 0) return;

    const int start = std::clamp(std::min(selectionStart_, selectionEnd_), 0, len);
    const int end = std::clamp(std::max(selectionStart_, selectionEnd_), 0, len);
    if (start != end) {
        expressionText_.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        cursorPos_ = start;
    } else {
        const int pos = std::clamp(cursorPos_, 0, len);
        if (pos > 0) {
            expressionText_.erase(static_cast<std::size_t>(pos - 1), 1);
            cursorPos_ = pos - 1;
        }
    }
    selectionStart_ = cursorPos_;
    selectionEnd_ = cursorPos_;
    pendingCursorPos_ = cursorPos_;
    requestFocusEditor_ = true;

    if (autoEvaluate_) {
        evaluateExpression(false);
    }
}

void MainWindow::loadPreset(const std::string& expr) {
    expressionText_ = expr;
    cursorPos_ = static_cast<int>(expressionText_.size());
    selectionStart_ = cursorPos_;
    selectionEnd_ = cursorPos_;
    pendingCursorPos_ = cursorPos_;
    evaluateExpression(true);
}

void MainWindow::syncVariableBuffers() {
    const auto entries = engine_.symbolTable().entries();
    varEditBuffers_.clear();
    varEditBuffers_.reserve(entries.size());
    for (const auto& e : entries) {
        VariableEditBuffer buf;
        buf.name = e.name;
        const std::string valStr = formatCompactNumber(e.value);
        std::snprintf(buf.valueText, sizeof(buf.valueText), "%s", valStr.c_str());
        varEditBuffers_.push_back(buf);
    }
}

void MainWindow::appendConsoleLog(bool isError, std::string summary, std::string detail) {
    consoleLogs_.push_front(ConsoleLogEntry{isError, currentTimestamp(), std::move(summary), std::move(detail)});
    if (consoleLogs_.size() > 60) {
        consoleLogs_.pop_back();
    }
}

void MainWindow::render() {
    const float dt = ImGui::GetIO().DeltaTime;
    if (copyToastTimer_ > 0.0) {
        copyToastTimer_ = std::max(0.0, copyToastTimer_ - static_cast<double>(dt));
    }

    if (shuntingPlaying_ && !lastResult_.statements.empty()) {
        const auto& stmt = lastResult_.statements[std::min(selectedStmtIndex_, lastResult_.statements.size() - 1)];
        if (!stmt.shuntingYardSteps.empty()) {
            shuntingPlayTimer_ += dt;
            if (shuntingPlayTimer_ >= 0.65f) {
                shuntingPlayTimer_ = 0.0f;
                if (shuntingStepIndex_ + 1 < stmt.shuntingYardSteps.size()) {
                    ++shuntingStepIndex_;
                } else {
                    shuntingPlaying_ = false;
                }
            }
        }
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * dpiScale_, 12.0f * dpiScale_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Palette::BgBase);

    if (ImGui::Begin("##MainStudioRoot", nullptr, rootFlags)) {
        renderHeaderBar();
        ImGui::Spacing();

        const float consoleHeight = showConsole_
                                        ? (lastResult_.error.has_value() ? 205.0f * dpiScale_ : 138.0f * dpiScale_)
                                        : (ImGui::GetFrameHeightWithSpacing() + 18.0f * dpiScale_);
        const float drawerWidth = showVariablesDrawer_ ? 340.0f * dpiScale_ : 0.0f;
        const float spacingX = showVariablesDrawer_ ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
        const float mainAreaHeight = std::max(260.0f * dpiScale_, ImGui::GetContentRegionAvail().y - consoleHeight - 8.0f * dpiScale_);
        const float leftColWidth = std::max(440.0f * dpiScale_, ImGui::GetContentRegionAvail().x - drawerWidth - spacingX);

        ImGui::BeginChild("##MainWorkspaceRow", ImVec2(0.0f, mainAreaHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        {
            ImGui::BeginChild("##CenterColumn", ImVec2(leftColWidth, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground);
            {
                renderExpressionEditorCard();
                ImGui::Spacing();
                renderResultAndProfilerCard();
                if (showKeypad_) {
                    ImGui::Spacing();
                    renderKeypadCard();
                }
                ImGui::Spacing();

                if (showPlotter_) {
                    const float availW = ImGui::GetContentRegionAvail().x;
                    const float inspectorW = std::max(320.0f * dpiScale_, availW * 0.54f);
                    ImGui::BeginChild("##InspectorWrap", ImVec2(inspectorW, 0.0f), ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NoBackground);
                    renderInspectorPanel();
                    ImGui::EndChild();

                    ImGui::SameLine();
                    renderPlotterPanel();
                } else {
                    renderInspectorPanel();
                }
            }
            ImGui::EndChild();

            if (showVariablesDrawer_) {
                ImGui::SameLine();
                renderVariablesDrawer();
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        renderErrorConsole();
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainWindow::renderHeaderBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##HeaderBar", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    {
        ImGui::AlignTextToFramePadding();
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(Palette::AccentCyan, "SCIENTIFIC EXPRESSION EVALUATOR");
        popFontSafe(fonts_.uiBold);

        ImGui::SameLine(0.0f, 10.0f * dpiScale_);
        drawPillBadge("AST + Stack VM + Symbolic", Palette::AccentIndigo, Palette::InfoBg);

        ImGui::SameLine(0.0f, 14.0f * dpiScale_);
        ImGui::SetNextItemWidth(235.0f * dpiScale_);
        if (ImGui::BeginCombo("##ExamplesCombo", "Load Sample Expression...")) {
            struct PresetItem { const char* label; const char* expr; };
            static constexpr PresetItem kPresets[] = {
                {"Hex, Binary & Sci (0xFF + 0b1010 + 1.5e2)", "0xFF + 0b1010 + 1.5e2 * sin(pi / 6)"},
                {"User Function (f(x) = x^2 + 2*x + 1; f(4))", "f(x) = x^2 + 2*x + 1\nf(4)"},
                {"Symbolic Derivative (diff(x^3 + 3*x^2 - 5*x, x))", "diff(x^3 + 3*x^2 - 5*x + 2, x)"},
                {"Chain-Rule Derivative (diff(sin(2*x), x))", "diff(sin(2 * x), x)"},
                {"Constant Folding & DCE (2+3*4; a=5; a=10; a*phi)", "2 + 3 * 4\na = 5\na = 10\na * phi"},
                {"Numerical Diff (ndiff(sin(x), x, 0))", "ndiff(sin(x), x, 0)"},
                {"[ERR] Unbalanced Parentheses: 5 + (3 * 2", "5 + (3 * 2"},
                {"[ERR] Consecutive Operators: 5 + * 2", "5 + * 2"},
                {"[ERR] Domain Error: ln(-5)", "ln(-5)"},
                {"[ERR] Missing Arguments: pow(2)", "pow(2)"},
            };
            for (const auto& item : kPresets) {
                if (ImGui::Selectable(item.label)) {
                    loadPreset(item.expr);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-Eval", &autoEvaluate_);
        ImGui::SameLine();
        if (ImGui::Checkbox("Optimize", &enableOptimization_)) {
            evaluateExpression(false);
        }

        const float rightControlsWidth = 315.0f * dpiScale_;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > rightControlsWidth) {
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - rightControlsWidth);
        } else {
            ImGui::SameLine();
        }

        auto drawToggleBtn = [](const char* label, bool& state) {
            if (state) {
                ImGui::PushStyleColor(ImGuiCol_Button, Palette::BgSelected);
                ImGui::PushStyleColor(ImGuiCol_Border, Palette::AccentCyan);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, Palette::BgElevated);
                ImGui::PushStyleColor(ImGuiCol_Border, Palette::BorderSubtle);
            }
            if (ImGui::Button(label)) {
                state = !state;
            }
            ImGui::PopStyleColor(2);
        };

        drawToggleBtn("Keypad", showKeypad_);
        ImGui::SameLine();
        drawToggleBtn("2D Plot", showPlotter_);
        ImGui::SameLine();
        drawToggleBtn("Symbols", showVariablesDrawer_);
        ImGui::SameLine();
        drawToggleBtn("Console", showConsole_);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderExpressionEditorCard() {
    const bool hasError = lastResult_.error.has_value();
    const ImVec4 borderCol = hasError
                                 ? Palette::AccentRose
                                 : (lastResult_.success && !lastResult_.emptyInput
                                        ? colorWithAlpha(Palette::AccentEmerald, 0.55f)
                                        : Palette::BorderSubtle);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
    ImGui::BeginChild("##EditorCard", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    {
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(Palette::TextSecondary, "EXPRESSION & COMMAND EDITOR");
        popFontSafe(fonts_.uiBold);

        ImGui::SameLine();
        ImGui::TextColored(Palette::TextMuted, "  (Enter: Evaluate  |  Ctrl+Enter: New Line)");

        const float btnAreaW = 256.0f * dpiScale_;
        const float editorW = std::max(220.0f * dpiScale_, ImGui::GetContentRegionAvail().x - btnAreaW - 8.0f * dpiScale_);

        if (requestFocusEditor_) {
            ImGui::SetKeyboardFocusHere();
            requestFocusEditor_ = false;
        }

        pushFontSafe(fonts_.monoRegular);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette::BgInput);
        ImGui::PushStyleColor(ImGuiCol_Border, Palette::BorderStrong);

        const ImVec2 editorPos = ImGui::GetCursorScreenPos();
        const ImVec2 editorSize(editorW, 60.0f * dpiScale_);
        const ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_CallbackResize |
            ImGuiInputTextFlags_CallbackAlways |
            ImGuiInputTextFlags_AllowTabInput |
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CtrlEnterForNewLine;

        if (expressionText_.capacity() < 256) {
            expressionText_.reserve(256);
        }

        const bool submitted = ImGui::InputTextMultiline(
            "##ExprInput",
            expressionText_.data(),
            expressionText_.capacity() + 1,
            editorSize,
            flags,
            expressionInputCallback,
            this);

        if (expressionText_.empty() && !ImGui::IsItemActive()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pad = ImGui::GetStyle().FramePadding;
            dl->AddText(
                ImVec2(editorPos.x + pad.x, editorPos.y + pad.y),
                toU32(Palette::TextMuted),
                "Enter expression, e.g. 0xFF + sin(pi/4), f(x) = x^2 + 1, or diff(x^2, x)...");
        }

        ImGui::PopStyleColor(2);
        popFontSafe(fonts_.monoRegular);

        if (submitted) {
            evaluateExpression(true);
        } else if (autoEvaluate_ && expressionText_ != lastEvaluatedText_) {
            evaluateExpression(false);
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.11f, 0.36f, 0.56f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.46f, 0.70f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.09f, 0.30f, 0.48f, 1.0f));
            if (ImGui::Button("Evaluate", ImVec2(106.0f * dpiScale_, 28.0f * dpiScale_))) {
                evaluateExpression(true);
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(66.0f * dpiScale_, 28.0f * dpiScale_))) {
                expressionText_.clear();
                cursorPos_ = 0;
                selectionStart_ = 0;
                selectionEnd_ = 0;
                pendingCursorPos_ = 0;
                evaluateExpression(false);
                requestFocusEditor_ = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("History", ImVec2(72.0f * dpiScale_, 28.0f * dpiScale_))) {
                ImGui::OpenPopup("##HistoryPopup");
            }
            if (ImGui::BeginPopup("##HistoryPopup")) {
                if (expressionHistory_.empty()) {
                    ImGui::TextDisabled("No evaluated history yet.");
                } else {
                    for (std::size_t i = 0; i < expressionHistory_.size(); ++i) {
                        std::string label = expressionHistory_[i];
                        std::replace(label.begin(), label.end(), '\n', ' ');
                        if (label.size() > 54) label = label.substr(0, 51) + "...";
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(label.c_str())) {
                            loadPreset(expressionHistory_[i]);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::TextColored(Palette::TextMuted, "Ln %d | %zu chars",
                               static_cast<int>(std::count(expressionText_.begin(), expressionText_.end(), '\n')) + 1,
                               expressionText_.size());
        }
        ImGui::EndGroup();

        renderAutocompleteBar();
        renderSyntaxHighlightPreview();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void MainWindow::renderAutocompleteBar() {
    const int len = static_cast<int>(expressionText_.size());
    const int pos = std::clamp(cursorPos_, 0, len);
    int start = pos;
    while (start > 0) {
        const char c = expressionText_[static_cast<std::size_t>(start - 1)];
        const bool okChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
        if (!okChar) break;
        --start;
    }

    std::string prefix;
    if (start < pos) {
        const char first = expressionText_[static_cast<std::size_t>(start)];
        if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_') {
            prefix = expressionText_.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(pos - start));
        }
    }

    struct Suggestion {
        std::string label;
        std::string insertText;
        std::string tooltip;
        ImVec4 color;
    };

    std::vector<Suggestion> matches;
    if (!prefix.empty()) {
        for (const auto& fn : FunctionRegistry::kBuiltInFunctions) {
            if (fn.name.rfind(prefix, 0) == 0 && fn.name != prefix) {
                matches.push_back(Suggestion{
                    std::string(fn.signature),
                    std::string(fn.name) + "(",
                    std::string(fn.description),
                    Palette::AccentCyan});
            }
        }
        for (const auto& ufn : engine_.symbolTable().functions()) {
            if (ufn.name.rfind(prefix, 0) == 0 && ufn.name != prefix) {
                matches.push_back(Suggestion{
                    ufn.name + "(...)",
                    ufn.name + "(",
                    "User function: " + ufn.bodyExpr,
                    Palette::AccentEmerald});
            }
        }
        for (const auto& sym : engine_.symbolTable().entries()) {
            if (sym.name.rfind(prefix, 0) == 0 && sym.name != prefix) {
                matches.push_back(Suggestion{
                    sym.name + " = " + formatCompactNumber(sym.value),
                    sym.name,
                    sym.description,
                    Palette::AccentPurple});
            }
        }
    }

    if (matches.empty()) {
        ImGui::TextColored(Palette::TextMuted,
                           "Built-ins: sin, cos, tan, sqrt, exp, ln, log10, pow, abs, diff, ndiff  |  Constants: pi, e, phi  |  0xFF / 0b101");
        return;
    }

    ImGui::TextColored(Palette::TextSecondary, "Autocomplete:");
    const std::size_t limit = std::min<std::size_t>(matches.size(), 6);
    for (std::size_t i = 0; i < limit; ++i) {
        ImGui::SameLine(0.0f, 6.0f * dpiScale_);
        const auto& s = matches[i];
        ImGui::PushStyleColor(ImGuiCol_Button, Palette::InfoBg);
        ImGui::PushStyleColor(ImGuiCol_Text, s.color);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(s.label.c_str())) {
            expressionText_.replace(static_cast<std::size_t>(start),
                                    static_cast<std::size_t>(pos - start),
                                    s.insertText);
            cursorPos_ = start + static_cast<int>(s.insertText.size());
            selectionStart_ = cursorPos_;
            selectionEnd_ = cursorPos_;
            pendingCursorPos_ = cursorPos_;
            requestFocusEditor_ = true;
            if (autoEvaluate_) {
                evaluateExpression(false);
            }
        }
        if (ImGui::IsItemHovered() && !s.tooltip.empty()) {
            ImGui::SetTooltip("%s", s.tooltip.c_str());
        }
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }
}

void MainWindow::renderSyntaxHighlightPreview() {
    pushFontSafe(fonts_.monoRegular);
    const float previewH = ImGui::GetTextLineHeight() + ImGui::GetStyle().WindowPadding.y * 1.3f + 6.0f * dpiScale_;
    popFontSafe(fonts_.monoRegular);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgInput);
    ImGui::BeginChild("##SyntaxPreview", ImVec2(0.0f, previewH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        pushFontSafe(fonts_.monoRegular);
        if (expressionText_.empty()) {
            ImGui::TextColored(Palette::TextMuted, "Syntax Highlight Preview: <empty>");
        } else {
            const auto tokens = highlightLexer_.tokenizeForHighlighting(expressionText_);
            const std::optional<SourceSpan> errSpan =
                lastResult_.error.has_value() ? std::optional<SourceSpan>(lastResult_.error->span)
                                              : std::nullopt;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float spaceAdvance = ImGui::CalcTextSize(" ").x;
            float maxX = cursor.x;

            std::size_t prevLine = 1;
            std::size_t prevEndCol = 1;
            bool first = true;

            for (const auto& tok : tokens) {
                if (tok.type == TokenType::EndOfInput) {
                    break;
                }
                if (tok.type == TokenType::Newline) {
                    dl->AddText(cursor, toU32(Palette::TextMuted), " ; ");
                    cursor.x += spaceAdvance * 3.0f;
                    prevLine = tok.span.line + 1;
                    prevEndCol = 1;
                    first = false;
                    continue;
                }

                if (!first && tok.span.line == prevLine && tok.span.column > prevEndCol) {
                    cursor.x += spaceAdvance * static_cast<float>(tok.span.column - prevEndCol);
                } else if (first && tok.span.column > 1) {
                    cursor.x += spaceAdvance * static_cast<float>(tok.span.column - 1);
                }

                const std::string& lex = tok.lexeme;
                const ImVec2 textSz = ImGui::CalcTextSize(lex.c_str());

                bool overlapsError = (tok.type == TokenType::Invalid);
                if (!overlapsError && errSpan.has_value() && tok.span.line == errSpan->line) {
                    const std::size_t tStart = tok.span.offset;
                    const std::size_t tEnd = tok.span.offset + tok.span.length;
                    const std::size_t eStart = errSpan->offset;
                    const std::size_t eEnd = errSpan->offset + std::max<std::size_t>(1, errSpan->length);
                    overlapsError = (tStart < eEnd && eStart < tEnd);
                }

                if (overlapsError) {
                    dl->AddRectFilled(
                        ImVec2(cursor.x - 2.0f, cursor.y - 1.0f),
                        ImVec2(cursor.x + textSz.x + 2.0f, cursor.y + textSz.y + 1.0f),
                        toU32(Palette::ErrorBg), 3.0f);
                    dl->AddLine(
                        ImVec2(cursor.x, cursor.y + textSz.y + 1.0f),
                        ImVec2(cursor.x + textSz.x, cursor.y + textSz.y + 1.0f),
                        toU32(Palette::AccentRose), 2.0f);
                }

                const ImVec4 col = overlapsError ? Palette::AccentRose : tokenSyntaxColor(tok);
                dl->AddText(cursor, toU32(col), lex.c_str());
                cursor.x += textSz.x;
                maxX = std::max(maxX, cursor.x);
                prevLine = tok.span.line;
                prevEndCol = tok.span.column + tok.span.length;
                first = false;
            }
            ImGui::Dummy(ImVec2(maxX - ImGui::GetCursorScreenPos().x + 8.0f, ImGui::GetTextLineHeight()));
        }
        popFontSafe(fonts_.monoRegular);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderResultAndProfilerCard() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##ResultCard", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    {
        const bool ok = lastResult_.success && !lastResult_.emptyInput;
        const bool err = lastResult_.error.has_value();

        if (err) {
            drawPillBadge("ERROR", Palette::AccentRose, Palette::ErrorBg);
        } else if (ok) {
            drawPillBadge("VM + AST OK", Palette::AccentEmerald, Palette::SuccessBg);
        } else {
            drawPillBadge("READY", Palette::TextSecondary, Palette::BgElevated);
        }

        ImGui::SameLine();
        char profBadge[128]{};
        std::snprintf(profBadge, sizeof(profBadge),
                      "VM: %.1f us | AST: %.1f us | Total: %.1f us",
                      lastResult_.profile.vmExecUs,
                      lastResult_.profile.astWalkExecUs,
                      lastResult_.profile.totalUs);
        drawPillBadge(profBadge, Palette::AccentCyan, Palette::InfoBg);

        if (ok && !lastResult_.statements.empty()) {
            const auto& activeStmt = lastResult_.statements[std::min(selectedStmtIndex_, lastResult_.statements.size() - 1)];
            ImGui::SameLine();
            char astBuf[96]{};
            std::snprintf(astBuf, sizeof(astBuf), "AST %zu->%zu | %zu ops",
                          activeStmt.rawNodeCount, activeStmt.optimizedNodeCount,
                          activeStmt.bytecode.instructions.size());
            drawPillBadge(astBuf, Palette::AccentPurple, Palette::BgElevated);
        }

        const float rightControlsW = 415.0f * dpiScale_;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > rightControlsW) {
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - rightControlsW);
        } else {
            ImGui::SameLine();
        }

        auto fmtBtn = [&](const char* label, NumberFormatMode mode) {
            const bool active = (formatMode_ == mode);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, Palette::BgSelected);
                ImGui::PushStyleColor(ImGuiCol_Border, Palette::AccentCyan);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, Palette::BgElevated);
                ImGui::PushStyleColor(ImGuiCol_Border, Palette::BorderSubtle);
            }
            if (ImGui::Button(label)) {
                formatMode_ = mode;
            }
            ImGui::PopStyleColor(2);
        };

        fmtBtn("Auto", NumberFormatMode::Auto);
        ImGui::SameLine(0, 4.0f * dpiScale_);
        fmtBtn("Fixed", NumberFormatMode::Fixed);
        ImGui::SameLine(0, 4.0f * dpiScale_);
        fmtBtn("Sci", NumberFormatMode::Scientific);
        ImGui::SameLine(0, 4.0f * dpiScale_);
        fmtBtn("Frac", NumberFormatMode::Fraction);

        ImGui::SameLine(0, 12.0f * dpiScale_);
        ImGui::TextColored(Palette::TextSecondary, "Prec:");
        ImGui::SameLine(0, 4.0f * dpiScale_);
        if (ImGui::Button("-##prec")) precision_ = std::max(0, precision_ - 1);
        ImGui::SameLine(0, 4.0f * dpiScale_);
        ImGui::Text("%d", precision_);
        ImGui::SameLine(0, 4.0f * dpiScale_);
        if (ImGui::Button("+##prec")) precision_ = std::min(15, precision_ + 1);

        std::string resultStr = "--";
        ImVec4 resultCol = Palette::TextMuted;
        if (err) {
            resultStr = "Error (Line " + std::to_string(lastResult_.error->span.line) +
                        ", Col " + std::to_string(lastResult_.error->span.column) + ")";
            resultCol = Palette::AccentRose;
        } else if (ok) {
            if (!lastResult_.symbolicResult.empty() && !lastResult_.hasNumericValue) {
                resultStr = lastResult_.symbolicResult;
                resultCol = Palette::AccentCyan;
            } else {
                resultStr = CompilerEngine::formatResult(lastResult_.finalValue, formatMode_, precision_);
                if (!lastResult_.statements.empty() &&
                    lastResult_.statements.back().evalOutput.wasAssignment) {
                    resultStr = lastResult_.statements.back().evalOutput.assignedSymbol + " = " + resultStr;
                }
                if (!lastResult_.symbolicResult.empty()) {
                    resultStr = lastResult_.symbolicResult + "   (= " + resultStr + ")";
                }
                resultCol = Palette::AccentEmerald;
            }
        }

        pushFontSafe(fonts_.monoLarge);
        ImGui::TextColored(resultCol, "%s", resultStr.c_str());
        popFontSafe(fonts_.monoLarge);

        if (ok && (lastResult_.hasNumericValue || !lastResult_.symbolicResult.empty())) {
            const float copyW = 82.0f * dpiScale_;
            ImGui::SameLine(std::max(10.0f, ImGui::GetWindowContentRegionMax().x - copyW));
            if (ImGui::Button(copyToastTimer_ > 0.0 ? "Copied!" : "Copy", ImVec2(copyW, 28.0f * dpiScale_))) {
                const std::string copyVal = lastResult_.hasNumericValue
                                                ? CompilerEngine::formatResult(lastResult_.finalValue, formatMode_, precision_)
                                                : lastResult_.symbolicResult;
                ImGui::SetClipboardText(copyVal.c_str());
                copyToastTimer_ = 1.5;
            }
        }
        if (!lastResult_.statusNote.empty()) {
            ImGui::TextColored(Palette::TextSecondary, "%s", lastResult_.statusNote.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderKeypadCard() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##KeypadCard", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    {
        struct KeyDef {
            const char* label;
            const char* insertStr;
            int kind;
        };

        static constexpr KeyDef kRows[4][10] = {
            {{"7","7",0}, {"8","8",0}, {"9","9",0}, {"/","/",1}, {"DEL","",3}, {"sin(","sin(",2}, {"cos(","cos(",2}, {"tan(","tan(",2}, {"^","^",1}, {"0x","0x",0}},
            {{"4","4",0}, {"5","5",0}, {"6","6",0}, {"*","*",1}, {"CLR","",4}, {"sqrt(","sqrt(",2}, {"ln(","ln(",2}, {"exp(","exp(",2}, {"%","%",1}, {"0b","0b",0}},
            {{"1","1",0}, {"2","2",0}, {"3","3",0}, {"-","-",1}, {"(","(",1}, {"pow(","pow(",2}, {"abs(","abs(",2}, {"diff(","diff(",2}, {"=","=",1}, {"phi","phi",2}},
            {{"0","0",0}, {".",".",0}, {",",",",1}, {"+","+",1}, {")",")",1}, {"pi","pi",2}, {"e","e",2}, {"x","x",0}, {"y","y",0}, {"EVAL","",5}},
        };

        const float totalW = ImGui::GetContentRegionAvail().x;
        const float gap = 5.0f * dpiScale_;
        const float btnW = std::max(40.0f * dpiScale_, (totalW - gap * 9.0f) / 10.0f);
        const float btnH = 27.0f * dpiScale_;

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 10; ++c) {
                const auto& key = kRows[r][c];
                if (c > 0) ImGui::SameLine(0.0f, gap);

                ImVec4 bg = Palette::BgElevated;
                ImVec4 fg = Palette::TextPrimary;
                if (key.kind == 1) {
                    bg = ImVec4(0.11f, 0.14f, 0.22f, 1.0f);
                    fg = Palette::AccentPink;
                } else if (key.kind == 2) {
                    bg = ImVec4(0.08f, 0.15f, 0.24f, 1.0f);
                    fg = Palette::AccentCyan;
                } else if (key.kind == 3 || key.kind == 4) {
                    bg = Palette::ErrorBg;
                    fg = Palette::AccentRose;
                } else if (key.kind == 5) {
                    bg = ImVec4(0.11f, 0.36f, 0.56f, 1.0f);
                    fg = Palette::TextPrimary;
                }

                ImGui::PushStyleColor(ImGuiCol_Button, bg);
                ImGui::PushStyleColor(ImGuiCol_Text, fg);
                if (ImGui::Button(key.label, ImVec2(btnW, btnH))) {
                    if (key.kind == 3) {
                        backspaceAtCursor();
                    } else if (key.kind == 4) {
                        expressionText_.clear();
                        cursorPos_ = 0;
                        selectionStart_ = 0;
                        selectionEnd_ = 0;
                        pendingCursorPos_ = 0;
                        evaluateExpression(false);
                        requestFocusEditor_ = true;
                    } else if (key.kind == 5) {
                        evaluateExpression(true);
                    } else {
                        insertTextAtCursor(key.insertStr);
                    }
                }
                ImGui::PopStyleColor(2);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderInspectorPanel() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##InspectorCard", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        if (lastResult_.statements.empty()) {
            ImGui::TextColored(Palette::TextSecondary, "EXECUTION PIPELINE & AST INSPECTOR");
            ImGui::Separator();
            if (lastResult_.error.has_value()) {
                ImGui::TextColored(Palette::AccentRose,
                                   "Compilation halted (%s).", lastResult_.error->errorCode.c_str());
                ImGui::TextWrapped("Inspect the Diagnostic Console below for exact column caret pointers.");
            } else {
                ImGui::TextColored(Palette::TextMuted, "Enter an expression above to inspect tokens, AST, bytecode, and timings.");
            }
        } else {
            if (lastResult_.statements.size() > 1) {
                ImGui::TextColored(Palette::TextSecondary, "Statement:");
                for (std::size_t i = 0; i < lastResult_.statements.size(); ++i) {
                    ImGui::SameLine();
                    char lbl[48]{};
                    std::snprintf(lbl, sizeof(lbl), "#%zu%s",
                                  i + 1,
                                  lastResult_.statements[i].eliminatedAsDeadCode ? " (DCE)" : "");
                    const bool active = (selectedStmtIndex_ == i);
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, Palette::BgSelected);
                    if (ImGui::Button(lbl)) {
                        selectedStmtIndex_ = i;
                        shuntingStepIndex_ = lastResult_.statements[i].shuntingYardSteps.empty()
                                                 ? 0
                                                 : lastResult_.statements[i].shuntingYardSteps.size() - 1;
                    }
                    if (active) ImGui::PopStyleColor();
                }
                ImGui::Separator();
            }

            selectedStmtIndex_ = std::min(selectedStmtIndex_, lastResult_.statements.size() - 1);
            const auto& stmt = lastResult_.statements[selectedStmtIndex_];

            if (ImGui::BeginTabBar("##InspectorTabs")) {
                if (ImGui::BeginTabItem("Token Stream")) {
                    renderTokenStreamTab(stmt);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Visual AST")) {
                    renderAstTab(stmt);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Bytecode VM")) {
                    renderBytecodeTab(stmt);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Shunting-Yard")) {
                    renderShuntingYardTab(stmt);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Profiler & Trace")) {
                    renderProfilerAndTraceTab(stmt);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderTokenStreamTab(const StatementArtifact& stmt) {
    ImGui::BeginChild("##TokenChipRibbon", ImVec2(0.0f, 36.0f * dpiScale_), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (std::size_t i = 0; i < stmt.infixTokens.size(); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 6.0f);
        const auto& tok = stmt.infixTokens[i];
        std::string chip = tok.displayLexeme() + " [" + tokenTypeName(tok.type) + "]";
        drawPillBadge(chip.c_str(), tokenSyntaxColor(tok), Palette::BgElevated);
    }
    ImGui::EndChild();

    const ImGuiTableFlags tFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("##TokenTable", 6, tFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0f * dpiScale_);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 105.0f * dpiScale_);
        ImGui::TableSetupColumn("Lexeme", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, 45.0f * dpiScale_);
        ImGui::TableSetupColumn("Cols", ImGuiTableColumnFlags_WidthFixed, 70.0f * dpiScale_);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 95.0f * dpiScale_);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < stmt.infixTokens.size(); ++i) {
            const auto& tok = stmt.infixTokens[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);

            ImGui::TableNextColumn();
            ImGui::TextColored(tokenSyntaxColor(tok), "%s", tokenTypeName(tok.type));

            ImGui::TableNextColumn();
            pushFontSafe(fonts_.monoRegular);
            ImGui::Text("%s", tok.displayLexeme().c_str());
            popFontSafe(fonts_.monoRegular);

            ImGui::TableNextColumn();
            ImGui::Text("%zu", tok.span.line);

            ImGui::TableNextColumn();
            ImGui::Text("%zu..%zu", tok.span.column, tok.span.endColumn());

            ImGui::TableNextColumn();
            if (tok.type == TokenType::Number) {
                ImGui::Text("%s", formatCompactNumber(tok.numericValue).c_str());
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderAstTab(const StatementArtifact& stmt) {
    const ASTNode* activeRoot =
        (showOptimizedAst_ && stmt.optimizedAst) ? stmt.optimizedAst.get() : stmt.rawAst.get();
    if (!activeRoot) {
        ImGui::TextDisabled("No AST available.");
        return;
    }

    ImGui::RadioButton("Graph", &astViewMode_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Tree", &astViewMode_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Split", &astViewMode_, 2);
    ImGui::SameLine(0.0f, 12.0f * dpiScale_);
    ImGui::Checkbox("Optimized AST", &showOptimizedAst_);

    if (astViewMode_ != 1) {
        ImGui::SameLine(0.0f, 12.0f * dpiScale_);
        ImGui::SetNextItemWidth(85.0f * dpiScale_);
        ImGui::SliderFloat("Zoom", &astZoom_, 0.5f, 2.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            astZoom_ = 1.0f;
            astPan_ = ImVec2(0.0f, 0.0f);
        }
    }

    if (!stmt.optimizationLog.empty()) {
        ImGui::SameLine();
        char optBadge[48]{};
        std::snprintf(optBadge, sizeof(optBadge), "%zu opt(s)", stmt.foldsPerformed);
        drawPillBadge(optBadge, Palette::AccentEmerald, Palette::SuccessBg);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            for (const auto& entry : stmt.optimizationLog) {
                ImGui::TextUnformatted(entry.c_str());
            }
            ImGui::EndTooltip();
        }
    }

    ImGui::Separator();

    if (astViewMode_ == 0) {
        renderAstGraphCanvas(*activeRoot);
    } else if (astViewMode_ == 1) {
        ImGui::BeginChild("##AstTreeOnly", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        renderAstTree(*activeRoot, 0);
        ImGui::EndChild();
    } else {
        const float graphW = ImGui::GetContentRegionAvail().x * 0.62f;
        ImGui::BeginChild("##AstGraphWrap", ImVec2(graphW, 0.0f), ImGuiChildFlags_None);
        renderAstGraphCanvas(*activeRoot);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##AstTreeSplit", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        ImGui::TextColored(Palette::TextSecondary, "AST Hierarchy");
        ImGui::Separator();
        renderAstTree(*activeRoot, 0);
        ImGui::EndChild();
    }
}

void MainWindow::renderAstGraphCanvas(const ASTNode& root) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgInput);
    ImGui::BeginChild("##AstCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ImGui::InvisibleButton("##AstCanvasHit",
                               ImVec2(std::max(1.0f, canvasSize.x), std::max(1.0f, canvasSize.y)),
                               ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            astPan_.x += delta.x;
            astPan_.y += delta.y;
        }
        if (ImGui::IsItemHovered()) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                astZoom_ = std::clamp(astZoom_ + wheel * 0.1f, 0.5f, 2.0f);
            }
        }

        std::vector<AstLayoutNode> layoutNodes;
        float leafCursor = 0.0f;
        int rootIdx = -1;
        layoutSubtree(&root, 0, leafCursor, layoutNodes, rootIdx);

        float minX = 0.0f;
        float maxX = 0.0f;
        for (std::size_t i = 0; i < layoutNodes.size(); ++i) {
            if (i == 0) {
                minX = maxX = layoutNodes[i].x;
            } else {
                minX = std::min(minX, layoutNodes[i].x);
                maxX = std::max(maxX, layoutNodes[i].x);
            }
        }

        const float gapX = 114.0f * astZoom_ * dpiScale_;
        const float gapY = 82.0f * astZoom_ * dpiScale_;
        const float baseNodeW = 92.0f * astZoom_ * dpiScale_;
        const float nodeH = 44.0f * astZoom_ * dpiScale_;

        const float midTreeX = 0.5f * (minX + maxX) * gapX;
        const ImVec2 origin(
            canvasPos.x + canvasSize.x * 0.5f - midTreeX + astPan_.x,
            canvasPos.y + 38.0f * dpiScale_ + astPan_.y);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        const float gridStep = 28.0f * astZoom_ * dpiScale_;
        const ImU32 dotCol = IM_COL32(55, 70, 96, 55);
        for (float gx = std::fmod(astPan_.x, gridStep); gx < canvasSize.x; gx += gridStep) {
            for (float gy = std::fmod(astPan_.y, gridStep); gy < canvasSize.y; gy += gridStep) {
                dl->AddCircleFilled(ImVec2(canvasPos.x + gx, canvasPos.y + gy), 1.0f, dotCol);
            }
        }

        for (const auto& ln : layoutNodes) {
            const ImVec2 pCenter(origin.x + ln.x * gapX, origin.y + ln.y * gapY);
            const ImVec2 pOut(pCenter.x, pCenter.y + nodeH * 0.5f);
            for (int cIdx : ln.childIndices) {
                const auto& ch = layoutNodes[static_cast<std::size_t>(cIdx)];
                const ImVec2 cCenter(origin.x + ch.x * gapX, origin.y + ch.y * gapY);
                const ImVec2 cIn(cCenter.x, cCenter.y - nodeH * 0.5f);
                const float ctrlOffset = 0.45f * (cIn.y - pOut.y);
                dl->AddBezierCubic(
                    pOut,
                    ImVec2(pOut.x, pOut.y + ctrlOffset),
                    ImVec2(cIn.x, cIn.y - ctrlOffset),
                    cIn,
                    toU32(Palette::BorderStrong),
                    2.0f);
            }
        }

        const ImVec2 mousePos = ImGui::GetMousePos();
        const ASTNode* hoveredAst = nullptr;
        for (const auto& ln : layoutNodes) {
            const char* kindTxt = ln.ast->isFoldedConstant() ? "Folded" : ln.ast->kindName();
            const ImVec2 kindSz = ImGui::CalcTextSize(kindTxt);
            const std::string lbl = ln.ast->label();
            const ImVec2 lblSz = ImGui::CalcTextSize(lbl.c_str());
            const float dynNodeW = std::max({baseNodeW, kindSz.x + 18.0f * dpiScale_, lblSz.x + 20.0f * dpiScale_});

            const ImVec2 center(origin.x + ln.x * gapX, origin.y + ln.y * gapY);
            const ImVec2 rectMin(center.x - dynNodeW * 0.5f, center.y - nodeH * 0.5f);
            const ImVec2 rectMax(center.x + dynNodeW * 0.5f, center.y + nodeH * 0.5f);

            const bool hovered =
                mousePos.x >= rectMin.x && mousePos.x <= rectMax.x &&
                mousePos.y >= rectMin.y && mousePos.y <= rectMax.y;
            if (hovered) {
                hoveredAst = ln.ast;
            }

            const ImVec4 accent = astNodeColor(*ln.ast);
            const ImVec4 fill = hovered ? Palette::BgHover : Palette::BgElevated;
            dl->AddRectFilled(rectMin, rectMax, toU32(fill), 8.0f);
            dl->AddRect(rectMin, rectMax, toU32(accent), 8.0f, 0, hovered ? 2.2f : 1.5f);

            dl->AddText(
                ImVec2(center.x - kindSz.x * 0.5f, rectMin.y + 4.0f * dpiScale_),
                toU32(accent),
                kindTxt);

            dl->AddText(
                ImVec2(center.x - lblSz.x * 0.5f, rectMin.y + nodeH * 0.48f),
                toU32(Palette::TextPrimary),
                lbl.c_str());
        }

        dl->PopClipRect();

        if (hoveredAst) {
            ImGui::BeginTooltip();
            ImGui::TextColored(astNodeColor(*hoveredAst), "%s: %s",
                               hoveredAst->kindName(), hoveredAst->label().c_str());
            ImGui::Text("Span: Line %zu, Col %zu..%zu",
                        hoveredAst->span().line, hoveredAst->span().column, hoveredAst->span().endColumn());
            if (hoveredAst->isFoldedConstant()) {
                ImGui::TextColored(Palette::AccentEmerald, "Constant-folded by AST optimizer");
            }
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderAstTree(const ASTNode& node, int depth) {
    ImGui::PushID(&node);
    const auto children = node.childNodes();
    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, astNodeColor(node));
    std::string caption = std::string(node.kindName()) + "  '" + node.label() + "'";
    if (node.isFoldedConstant()) {
        caption += "  [folded]";
    }
    const bool open = ImGui::TreeNodeEx(caption.c_str(), flags);
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Line %zu, Col %zu..%zu (Depth %d)",
                          node.span().line, node.span().column, node.span().endColumn(), depth);
    }

    if (open && !children.empty()) {
        for (const ASTNode* child : children) {
            renderAstTree(*child, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void MainWindow::renderBytecodeTab(const StatementArtifact& stmt) {
    if (stmt.eliminatedAsDeadCode) {
        ImGui::TextColored(Palette::AccentAmber, "Statement eliminated by Dead-Code Elimination pass.");
        return;
    }

    ImGui::TextColored(Palette::TextSecondary, "Stack VM Disassembly (%zu instructions)",
                       stmt.bytecode.instructions.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Assembly")) {
        ImGui::SetClipboardText(stmt.bytecode.disassemble().c_str());
    }
    ImGui::Separator();

    const ImGuiTableFlags tFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("##BytecodeTable", 4, tFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("PC", ImGuiTableColumnFlags_WidthFixed, 56.0f * dpiScale_);
        ImGui::TableSetupColumn("OpCode", ImGuiTableColumnFlags_WidthFixed, 92.0f * dpiScale_);
        ImGui::TableSetupColumn("Operand / Target", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Source Span", ImGuiTableColumnFlags_WidthFixed, 95.0f * dpiScale_);
        ImGui::TableHeadersRow();

        for (const auto& ins : stmt.bytecode.instructions) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            pushFontSafe(fonts_.monoRegular);
            ImGui::Text("%04zu", ins.address);
            popFontSafe(fonts_.monoRegular);

            ImGui::TableNextColumn();
            pushFontSafe(fonts_.monoRegular);
            ImGui::TextColored(opcodeColor(ins.op), "%s", opCodeName(ins.op));
            popFontSafe(fonts_.monoRegular);

            ImGui::TableNextColumn();
            pushFontSafe(fonts_.monoRegular);
            const std::string opText = BytecodeProgram::formatInstructionOperand(ins);
            ImGui::TextUnformatted(opText.empty() ? "-" : opText.c_str());
            popFontSafe(fonts_.monoRegular);

            ImGui::TableNextColumn();
            ImGui::Text("L%zu:%zu", ins.span.line, ins.span.column);
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderShuntingYardTab(const StatementArtifact& stmt) {
    ImGui::TextColored(Palette::TextSecondary, "Postfix (RPN):");
    for (std::size_t i = 0; i < stmt.rpnTokens.size(); ++i) {
        ImGui::SameLine(0.0f, 6.0f);
        const auto& tok = stmt.rpnTokens[i];
        drawPillBadge(tok.displayLexeme().c_str(), tokenSyntaxColor(tok), Palette::BgElevated);
    }

    if (stmt.shuntingYardSteps.empty()) {
        ImGui::TextDisabled("No Shunting-Yard steps recorded.");
        return;
    }

    shuntingStepIndex_ = std::min(shuntingStepIndex_, stmt.shuntingYardSteps.size() - 1);
    const auto& step = stmt.shuntingYardSteps[shuntingStepIndex_];

    ImGui::Separator();
    if (ImGui::Button("|<<")) {
        shuntingStepIndex_ = 0;
        shuntingPlaying_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("< Prev") && shuntingStepIndex_ > 0) {
        --shuntingStepIndex_;
        shuntingPlaying_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(shuntingPlaying_ ? "Pause" : "Play")) {
        if (!shuntingPlaying_ && shuntingStepIndex_ + 1 >= stmt.shuntingYardSteps.size()) {
            shuntingStepIndex_ = 0;
        }
        shuntingPlaying_ = !shuntingPlaying_;
        shuntingPlayTimer_ = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next >") && shuntingStepIndex_ + 1 < stmt.shuntingYardSteps.size()) {
        ++shuntingStepIndex_;
        shuntingPlaying_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(">>|")) {
        shuntingStepIndex_ = stmt.shuntingYardSteps.size() - 1;
        shuntingPlaying_ = false;
    }
    ImGui::SameLine();
    int sliderIdx = static_cast<int>(shuntingStepIndex_ + 1);
    ImGui::SetNextItemWidth(130.0f * dpiScale_);
    if (ImGui::SliderInt("##StepSlider", &sliderIdx, 1, static_cast<int>(stmt.shuntingYardSteps.size()), "Step %d")) {
        shuntingStepIndex_ = static_cast<std::size_t>(sliderIdx - 1);
        shuntingPlaying_ = false;
    }
    ImGui::SameLine();
    ImGui::TextColored(Palette::AccentCyan, "%s", step.action.c_str());

    const float halfW = std::max(160.0f * dpiScale_, ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::BeginChild("##OpStackBox", ImVec2(halfW, 110.0f * dpiScale_), ImGuiChildFlags_Borders);
    {
        ImGui::TextColored(Palette::AccentPink, "Operator Stack");
        ImGui::Separator();
        if (step.operatorStack.empty()) {
            ImGui::TextDisabled("<empty>");
        } else {
            for (std::size_t i = step.operatorStack.size(); i > 0; --i) {
                const bool isTop = (i == step.operatorStack.size());
                std::string label = step.operatorStack[i - 1] + (isTop ? "  [TOP]" : "");
                drawPillBadge(label.c_str(),
                              isTop ? Palette::AccentCyan : Palette::AccentPink,
                              isTop ? Palette::InfoBg : Palette::BgElevated);
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##OutQueueBox", ImVec2(0.0f, 110.0f * dpiScale_), ImGuiChildFlags_Borders);
    {
        ImGui::TextColored(Palette::AccentAmber, "Output RPN Queue");
        ImGui::Separator();
        if (step.outputQueue.empty()) {
            ImGui::TextDisabled("<empty>");
        } else {
            const float wrapX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            for (std::size_t i = 0; i < step.outputQueue.size(); ++i) {
                if (i > 0 && ImGui::GetItemRectMax().x + 65.0f * dpiScale_ < wrapX) {
                    ImGui::SameLine(0.0f, 6.0f);
                }
                drawPillBadge(step.outputQueue[i].c_str(), Palette::TextPrimary, Palette::BgElevated);
            }
        }
    }
    ImGui::EndChild();

    const ImGuiTableFlags tFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("##ShuntingStepsTable", 4, tFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 44.0f * dpiScale_);
        ImGui::TableSetupColumn("Token", ImGuiTableColumnFlags_WidthFixed, 64.0f * dpiScale_);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Output RPN", ImGuiTableColumnFlags_WidthFixed, 160.0f * dpiScale_);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < stmt.shuntingYardSteps.size(); ++i) {
            const auto& s = stmt.shuntingYardSteps[i];
            ImGui::TableNextRow();
            if (i == shuntingStepIndex_) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, toU32(Palette::BgSelected));
            }
            ImGui::TableNextColumn();
            char rowId[32]{};
            std::snprintf(rowId, sizeof(rowId), "%zu##sy", s.stepIndex);
            if (ImGui::Selectable(rowId, i == shuntingStepIndex_, ImGuiSelectableFlags_SpanAllColumns)) {
                shuntingStepIndex_ = i;
                shuntingPlaying_ = false;
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(tokenSyntaxColor(s.currentToken), "%s",
                               s.currentToken.displayLexeme().c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.action.c_str());
            ImGui::TableNextColumn();
            std::string outJoin;
            for (std::size_t k = 0; k < s.outputQueue.size(); ++k) {
                if (k > 0) outJoin += ' ';
                outJoin += s.outputQueue[k];
            }
            ImGui::TextUnformatted(outJoin.empty() ? "-" : outJoin.c_str());
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderProfilerAndTraceTab(const StatementArtifact& stmt) {
    ImGui::BeginChild("##ProfilerBreakdown", ImVec2(0.0f, 128.0f * dpiScale_), ImGuiChildFlags_Borders);
    {
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(Palette::AccentCyan, "EXECUTION PROFILER (Microseconds / Nanoseconds)");
        popFontSafe(fonts_.uiBold);
        ImGui::Separator();

        const double totalUs = std::max(0.001, lastResult_.profile.totalUs);
        struct StageRow { const char* name; double us; ImVec4 col; };
        const StageRow rows[] = {
            {"1. Lexer",            lastResult_.profile.lexUs,             Palette::AccentAmber},
            {"2. Shunting Parser",  lastResult_.profile.parseUs,           Palette::AccentIndigo},
            {"3. AST Optimizer",    lastResult_.profile.optimizeUs,        Palette::AccentPurple},
            {"4. Bytecode Compile", lastResult_.profile.bytecodeCompileUs, Palette::AccentPink},
            {"5. Stack VM Exec",    lastResult_.profile.vmExecUs,          Palette::AccentEmerald},
            {"6. AST-Walker Exec",  lastResult_.profile.astWalkExecUs,     Palette::AccentCyan},
        };

        if (ImGui::BeginTable("##ProfTable", 4, ImGuiTableFlags_SizingStretchSame)) {
            for (const auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(r.col, "%s", r.name);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f us (%.0f ns)", r.us, r.us * 1000.0);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f%%", 100.0 * r.us / totalUs);
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, r.col);
                ImGui::ProgressBar(static_cast<float>(std::clamp(r.us / totalUs, 0.0, 1.0)),
                                   ImVec2(-FLT_MIN, 12.0f * dpiScale_), "");
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();

    const ImGuiTableFlags tFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (ImGui::BeginTable("##EvalTraceTable", 5, tFlags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 46.0f * dpiScale_);
        ImGui::TableSetupColumn("Depth", ImGuiTableColumnFlags_WidthFixed, 50.0f * dpiScale_);
        ImGui::TableSetupColumn("AST Evaluation Trace", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 115.0f * dpiScale_);
        ImGui::TableSetupColumn("Line:Col", ImGuiTableColumnFlags_WidthFixed, 76.0f * dpiScale_);
        ImGui::TableHeadersRow();

        for (const auto& step : stmt.evalOutput.trace) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", step.stepIndex);
            ImGui::TableNextColumn();
            ImGui::Text("%d", step.depth);
            ImGui::TableNextColumn();
            pushFontSafe(fonts_.monoRegular);
            const std::string indent(static_cast<std::size_t>(step.depth) * 2, ' ');
            ImGui::Text("%s%s", indent.c_str(), step.operation.c_str());
            popFontSafe(fonts_.monoRegular);
            ImGui::TableNextColumn();
            ImGui::TextColored(Palette::AccentEmerald, "%s",
                               CompilerEngine::formatResult(step.value, formatMode_, precision_).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu:%zu", step.span.line, step.span.column);
        }
        ImGui::EndTable();
    }
}

void MainWindow::renderPlotterPanel() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##PlotterPanel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(Palette::AccentCyan, "2D FUNCTION PLOTTER");
        popFontSafe(fonts_.uiBold);

        if (currentPlot_.valid) {
            ImGui::SameLine();
            char pTime[48]{};
            std::snprintf(pTime, sizeof(pTime), "%.0f us", currentPlot_.sampleTimeUs);
            drawPillBadge(pTime, Palette::AccentEmerald, Palette::SuccessBg);
        }

        const float btnW = 54.0f * dpiScale_;
        const float syncBtnW = 86.0f * dpiScale_;
        ImGui::SetNextItemWidth(std::max(100.0f * dpiScale_,
                                         ImGui::GetContentRegionAvail().x - btnW - syncBtnW - 14.0f * dpiScale_));
        const bool enterPlot = ImGui::InputTextWithHint(
            "##PlotExpr", "f(x) e.g. sin(x)", plotExprBuf_, sizeof(plotExprBuf_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Plot", ImVec2(btnW, 0.0f)) || enterPlot) {
            refreshPlot();
        }
        ImGui::SameLine();
        if (ImGui::Button("From Input", ImVec2(syncBtnW, 0.0f))) {
            std::snprintf(plotExprBuf_, sizeof(plotExprBuf_), "%s", expressionText_.c_str());
            refreshPlot();
        }

        // Range & options bar
        ImGui::SetNextItemWidth(62.0f * dpiScale_);
        if (ImGui::InputDouble("xMin", &plotXMin_, 0.0, 0.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            refreshPlot();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(62.0f * dpiScale_);
        if (ImGui::InputDouble("xMax", &plotXMax_, 0.0, 0.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            refreshPlot();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto Y", &plotAutoY_)) {
            refreshPlot();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("f'(x)", &plotShowDerivative_)) {
            refreshPlot();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset")) {
            plotXMin_ = -10.0;
            plotXMax_ = 10.0;
            plotAutoY_ = true;
            refreshPlot();
        }

        if (!currentPlot_.errorMessage.empty()) {
            ImGui::TextColored(Palette::AccentRose, "%s", currentPlot_.errorMessage.c_str());
        }

        // Cartesian Canvas
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgInput);
        ImGui::BeginChild("##PlotCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size = ImGui::GetContentRegionAvail();
            const float w = std::max(10.0f, size.x);
            const float h = std::max(10.0f, size.y);

            ImGui::InvisibleButton("##PlotHit", ImVec2(w, h), ImGuiButtonFlags_MouseButtonLeft);

            const double xSpan = std::max(1e-9, plotXMax_ - plotXMin_);
            const double ySpan = std::max(1e-9, plotYMax_ - plotYMin_);

            auto worldToScreen = [&](double wx, double wy) -> ImVec2 {
                const float sx = pos.x + static_cast<float>((wx - plotXMin_) / xSpan) * w;
                const float sy = pos.y + h - static_cast<float>((wy - plotYMin_) / ySpan) * h;
                return ImVec2(sx, sy);
            };

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                const double dxWorld = -static_cast<double>(delta.x) / w * xSpan;
                const double dyWorld = static_cast<double>(delta.y) / h * ySpan;
                plotXMin_ += dxWorld;
                plotXMax_ += dxWorld;
                plotAutoY_ = false;
                plotYMin_ += dyWorld;
                plotYMax_ += dyWorld;
                refreshPlot();
            }

            if (ImGui::IsItemHovered()) {
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) {
                    const ImVec2 mp = ImGui::GetMousePos();
                    const double rx = std::clamp(static_cast<double>(mp.x - pos.x) / w, 0.0, 1.0);
                    const double ry = std::clamp(1.0 - static_cast<double>(mp.y - pos.y) / h, 0.0, 1.0);
                    const double focusX = plotXMin_ + rx * xSpan;
                    const double focusY = plotYMin_ + ry * ySpan;
                    const double factor = wheel > 0.0f ? 0.85 : 1.18;
                    const double newXSpan = std::clamp(xSpan * factor, 1e-4, 1e6);
                    const double newYSpan = std::clamp(ySpan * factor, 1e-4, 1e6);
                    plotXMin_ = focusX - rx * newXSpan;
                    plotXMax_ = plotXMin_ + newXSpan;
                    plotAutoY_ = false;
                    plotYMin_ = focusY - ry * newYSpan;
                    plotYMax_ = plotYMin_ + newYSpan;
                    refreshPlot();
                }
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(pos, ImVec2(pos.x + w, pos.y + h), true);

            // Grid lines & tick labels
            const double xStep = computeNiceStep(xSpan, 6);
            const double yStep = computeNiceStep(ySpan, 5);
            const ImU32 gridCol = IM_COL32(38, 49, 68, 180);
            const ImU32 axisCol = IM_COL32(120, 140, 175, 220);

            const double firstX = std::ceil(plotXMin_ / xStep) * xStep;
            for (double gx = firstX; gx <= plotXMax_ + 1e-9; gx += xStep) {
                const ImVec2 p0 = worldToScreen(gx, plotYMin_);
                const ImVec2 p1 = worldToScreen(gx, plotYMax_);
                dl->AddLine(p0, p1, gridCol, 1.0f);
                char tick[24]{};
                std::snprintf(tick, sizeof(tick), "%.3g", std::fabs(gx) < 1e-12 ? 0.0 : gx);
                dl->AddText(ImVec2(p0.x + 3.0f, pos.y + h - 16.0f * dpiScale_),
                            toU32(Palette::TextMuted), tick);
            }

            const double firstY = std::ceil(plotYMin_ / yStep) * yStep;
            for (double gy = firstY; gy <= plotYMax_ + 1e-9; gy += yStep) {
                const ImVec2 p0 = worldToScreen(plotXMin_, gy);
                const ImVec2 p1 = worldToScreen(plotXMax_, gy);
                dl->AddLine(p0, p1, gridCol, 1.0f);
                char tick[24]{};
                std::snprintf(tick, sizeof(tick), "%.3g", std::fabs(gy) < 1e-12 ? 0.0 : gy);
                dl->AddText(ImVec2(pos.x + 4.0f, p0.y - 14.0f * dpiScale_),
                            toU32(Palette::TextMuted), tick);
            }

            if (plotXMin_ <= 0.0 && plotXMax_ >= 0.0) {
                const ImVec2 a0 = worldToScreen(0.0, plotYMin_);
                const ImVec2 a1 = worldToScreen(0.0, plotYMax_);
                dl->AddLine(a0, a1, axisCol, 1.6f);
            }
            if (plotYMin_ <= 0.0 && plotYMax_ >= 0.0) {
                const ImVec2 a0 = worldToScreen(plotXMin_, 0.0);
                const ImVec2 a1 = worldToScreen(plotXMax_, 0.0);
                dl->AddLine(a0, a1, axisCol, 1.6f);
            }

            auto drawCurve = [&](const std::vector<PlotPoint>& pts, ImU32 color, float thickness) {
                const double jumpLimit = ySpan * 3.5;
                bool hasPrev = false;
                ImVec2 prevScreen{};
                double prevY = 0.0;
                for (const auto& pt : pts) {
                    if (!pt.valid) {
                        hasPrev = false;
                        continue;
                    }
                    const ImVec2 curScreen = worldToScreen(pt.x, pt.y);
                    if (hasPrev && std::fabs(pt.y - prevY) <= jumpLimit) {
                        dl->AddLine(prevScreen, curScreen, color, thickness);
                    }
                    prevScreen = curScreen;
                    prevY = pt.y;
                    hasPrev = true;
                }
            };

            if (currentPlot_.valid) {
                if (plotShowDerivative_ && !currentPlot_.derivativePoints.empty()) {
                    drawCurve(currentPlot_.derivativePoints, toU32(colorWithAlpha(Palette::AccentAmber, 0.85f)), 1.7f);
                }
                drawCurve(currentPlot_.points, toU32(Palette::AccentCyan), 2.3f);
            }

            // Legend
            if (currentPlot_.valid) {
                const float lx = pos.x + 10.0f * dpiScale_;
                float ly = pos.y + 8.0f * dpiScale_;
                std::string fLbl = "y = " + currentPlot_.expressionLabel;
                dl->AddText(ImVec2(lx, ly), toU32(Palette::AccentCyan), fLbl.c_str());
                if (plotShowDerivative_ && !currentPlot_.derivativeLabel.empty()) {
                    ly += ImGui::GetTextLineHeight() + 2.0f;
                    std::string dLbl = "dy/dx = " + currentPlot_.derivativeLabel;
                    dl->AddText(ImVec2(lx, ly), toU32(Palette::AccentAmber), dLbl.c_str());
                }
            }

            // Crosshair hover readout
            if (ImGui::IsItemHovered() && currentPlot_.valid) {
                const ImVec2 mp = ImGui::GetMousePos();
                const double wx = plotXMin_ + (static_cast<double>(mp.x - pos.x) / w) * xSpan;
                dl->AddLine(ImVec2(mp.x, pos.y), ImVec2(mp.x, pos.y + h), IM_COL32(148, 163, 184, 90), 1.0f);
                dl->AddLine(ImVec2(pos.x, mp.y), ImVec2(pos.x + w, mp.y), IM_COL32(148, 163, 184, 90), 1.0f);

                const auto yVal = plotter_.evaluateAt(plotExprBuf_, engine_.symbolTable(), wx);
                if (yVal.has_value()) {
                    const ImVec2 dotPos = worldToScreen(wx, *yVal);
                    dl->AddCircleFilled(dotPos, 4.5f * dpiScale_, toU32(Palette::AccentCyan));
                    char readout[96]{};
                    std::snprintf(readout, sizeof(readout), "x = %.4g,  y = %.6g", wx, *yVal);
                    dl->AddRectFilled(
                        ImVec2(pos.x + 8.0f * dpiScale_, pos.y + h - 38.0f * dpiScale_),
                        ImVec2(pos.x + 230.0f * dpiScale_, pos.y + h - 18.0f * dpiScale_),
                        toU32(colorWithAlpha(Palette::BgPanel, 0.9f)),
                        4.0f);
                    dl->AddText(ImVec2(pos.x + 14.0f * dpiScale_, pos.y + h - 35.0f * dpiScale_),
                                toU32(Palette::TextPrimary), readout);
                }
            }

            dl->PopClipRect();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderVariablesDrawer() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    ImGui::BeginChild("##VariablesDrawer", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(Palette::AccentPurple, "MEMORY & SYMBOL INSPECTOR");
        popFontSafe(fonts_.uiBold);
        ImGui::Separator();

        const float availFormW = ImGui::GetContentRegionAvail().x;
        const float setBtnW = 54.0f * dpiScale_;
        const float formGap = ImGui::GetStyle().ItemSpacing.x;
        const float nameW = std::max(72.0f * dpiScale_, (availFormW - setBtnW - formGap * 2.0f) * 0.42f);
        const float valW = std::max(80.0f * dpiScale_, availFormW - nameW - setBtnW - formGap * 2.0f);

        ImGui::SetNextItemWidth(nameW);
        ImGui::InputTextWithHint("##VarName", "name", newVarName_, sizeof(newVarName_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(valW);
        const bool enterValue = ImGui::InputTextWithHint(
            "##VarVal", "value", newVarValue_, sizeof(newVarValue_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Set", ImVec2(setBtnW, 0.0f)) || enterValue) {
            const std::string nameStr(newVarName_);
            const std::string valStr(newVarValue_);
            double parsedVal = 0.0;
            const char* first = valStr.data();
            const char* last = first + valStr.size();
            auto [ptr, ec] = std::from_chars(first, last, parsedVal);

            if (!SymbolTable::isAssignableIdentifier(nameStr)) {
                varFeedbackError_ = "Invalid or reserved symbol name.";
            } else if (valStr.empty() || ec != std::errc{} || ptr != last || !std::isfinite(parsedVal)) {
                varFeedbackError_ = "Enter a valid finite number.";
            } else {
                engine_.symbolTable().set(nameStr, parsedVal);
                varFeedbackError_.clear();
                appendConsoleLog(false, "Symbol set: " + nameStr + " = " + formatCompactNumber(parsedVal));
                evaluateExpression(false);
            }
        }

        if (!varFeedbackError_.empty()) {
            ImGui::TextColored(Palette::AccentRose, "%s", varFeedbackError_.c_str());
        }

        const float resetBtnW = 96.0f * dpiScale_;
        ImGui::SetNextItemWidth(std::max(80.0f * dpiScale_, ImGui::GetContentRegionAvail().x - resetBtnW - ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputTextWithHint("##VarFilter", "Filter symbols...", varFilter_, sizeof(varFilter_));
        ImGui::SameLine();
        if (ImGui::Button("Reset All", ImVec2(resetBtnW, 0.0f))) {
            engine_.symbolTable().resetToDefaults();
            varFeedbackError_.clear();
            appendConsoleLog(false, "Symbol table reset to constants (pi, e, phi).");
            evaluateExpression(false);
        }

        ImGui::Spacing();

        const auto userFuncs = engine_.symbolTable().functions();
        const float varTableH = userFuncs.empty() ? 0.0f : ImGui::GetContentRegionAvail().y * 0.62f;

        const ImGuiTableFlags tFlags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("##VariablesTable", 4, tFlags, ImVec2(0.0f, varTableH))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 66.0f * dpiScale_);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 50.0f * dpiScale_);
            ImGui::TableSetupColumn("##Act", ImGuiTableColumnFlags_WidthFixed, 32.0f * dpiScale_);
            ImGui::TableHeadersRow();

            const auto entries = engine_.symbolTable().entries();
            const std::string filterStr(varFilter_);

            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                if (!filterStr.empty() && e.name.find(filterStr) == std::string::npos) continue;
                if (i >= varEditBuffers_.size() || varEditBuffers_[i].name != e.name) continue;

                ImGui::PushID(e.name.c_str());
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(e.isBuiltIn ? Palette::AccentPurple : Palette::TextPrimary,
                                   "%s", e.name.c_str());

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool committed = ImGui::InputText(
                    "##ValEdit",
                    varEditBuffers_[i].valueText,
                    sizeof(varEditBuffers_[i].valueText),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                if (committed || ImGui::IsItemDeactivatedAfterEdit()) {
                    const std::string valStr(varEditBuffers_[i].valueText);
                    double parsedVal = 0.0;
                    const char* first = valStr.data();
                    const char* last = first + valStr.size();
                    auto [ptr, ec] = std::from_chars(first, last, parsedVal);
                    if (!valStr.empty() && ec == std::errc{} && ptr == last && std::isfinite(parsedVal)) {
                        engine_.symbolTable().set(e.name, parsedVal);
                        varFeedbackError_.clear();
                        evaluateExpression(false);
                    } else {
                        const std::string restore = formatCompactNumber(e.value);
                        std::snprintf(varEditBuffers_[i].valueText,
                                      sizeof(varEditBuffers_[i].valueText), "%s", restore.c_str());
                    }
                }

                ImGui::TableNextColumn();
                if (e.isBuiltIn && e.isOverridden) ImGui::TextColored(Palette::AccentAmber, "Edited");
                else if (e.isBuiltIn) ImGui::TextColored(Palette::AccentPurple, "Const");
                else ImGui::TextColored(Palette::AccentCyan, "User");

                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, Palette::ErrorBg);
                ImGui::PushStyleColor(ImGuiCol_Text, Palette::AccentRose);
                if (ImGui::Button("X", ImVec2(24.0f * dpiScale_, 0.0f))) {
                    engine_.symbolTable().remove(e.name);
                    appendConsoleLog(false, "Removed variable '" + e.name + "'");
                    evaluateExpression(false);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (!userFuncs.empty()) {
            ImGui::Spacing();
            pushFontSafe(fonts_.uiBold);
            ImGui::TextColored(Palette::AccentEmerald, "USER-DEFINED FUNCTIONS");
            popFontSafe(fonts_.uiBold);

            if (ImGui::BeginTable("##UserFuncsTable", 3, tFlags)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Signature", ImGuiTableColumnFlags_WidthFixed, 84.0f * dpiScale_);
                ImGui::TableSetupColumn("Definition", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##Act", ImGuiTableColumnFlags_WidthFixed, 68.0f * dpiScale_);
                ImGui::TableHeadersRow();

                for (const auto& fn : userFuncs) {
                    ImGui::PushID(fn.name.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    std::string sig = fn.name + "(";
                    for (std::size_t k = 0; k < fn.params.size(); ++k) {
                        if (k > 0) sig += ",";
                        sig += fn.params[k];
                    }
                    sig += ")";
                    ImGui::TextColored(Palette::AccentEmerald, "%s", sig.c_str());

                    ImGui::TableNextColumn();
                    pushFontSafe(fonts_.monoRegular);
                    ImGui::Text("%s", fn.bodyExpr.c_str());
                    popFontSafe(fonts_.monoRegular);

                    ImGui::TableNextColumn();
                    if (fn.params.size() == 1 && ImGui::SmallButton("Plot")) {
                        std::snprintf(plotExprBuf_, sizeof(plotExprBuf_), "%s", fn.bodyExpr.c_str());
                        refreshPlot();
                    }
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, Palette::ErrorBg);
                    ImGui::PushStyleColor(ImGuiCol_Text, Palette::AccentRose);
                    if (ImGui::SmallButton("X")) {
                        engine_.symbolTable().removeFunction(fn.name);
                        evaluateExpression(false);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void MainWindow::renderErrorConsole() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::BgPanel);
    const bool hasErr = lastResult_.error.has_value();
    ImGui::PushStyleColor(ImGuiCol_Border, hasErr ? Palette::AccentRose : Palette::BorderSubtle);

    ImGui::BeginChild("##ErrorConsole", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    {
        pushFontSafe(fonts_.uiBold);
        ImGui::TextColored(hasErr ? Palette::AccentRose : Palette::TextSecondary,
                           "DIAGNOSTIC CONSOLE");
        popFontSafe(fonts_.uiBold);

        ImGui::SameLine();
        if (hasErr) {
            const std::string badge = errorPhaseToString(lastResult_.error->phase) + " Error";
            drawPillBadge(badge.c_str(), Palette::AccentRose, Palette::ErrorBg);
        } else {
            drawPillBadge("0 Errors", Palette::AccentEmerald, Palette::SuccessBg);
        }

        const float consoleBtnsW = 164.0f * dpiScale_;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > consoleBtnsW) {
            ImGui::SameLine(ImGui::GetCursorPosX() + avail - consoleBtnsW);
        } else {
            ImGui::SameLine();
        }
        if (ImGui::Button("Clear Log")) {
            consoleLogs_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button(showConsole_ ? "Collapse" : "Expand")) {
            showConsole_ = !showConsole_;
        }

        if (showConsole_) {
            ImGui::Separator();
            ImGui::BeginChild("##ConsoleScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
            {
                if (hasErr) {
                    const auto& err = *lastResult_.error;
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, Palette::ErrorBg);
                    ImGui::PushStyleColor(ImGuiCol_Border, Palette::AccentRose);
                    ImGui::BeginChild("##ActiveErrorCard", ImVec2(0.0f, 0.0f),
                                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
                    {
                        pushFontSafe(fonts_.uiBold);
                        ImGui::TextColored(Palette::AccentRose,
                                           "[%sError / %s] Line %zu, Col %zu..%zu: %s",
                                           errorPhaseToString(err.phase).c_str(),
                                           err.errorCode.c_str(),
                                           err.span.line,
                                           err.span.column,
                                           err.span.endColumn(),
                                           err.message.c_str());
                        popFontSafe(fonts_.uiBold);

                        pushFontSafe(fonts_.monoRegular);
                        ImGui::TextColored(Palette::AccentRose, "%s", err.caretBlock.c_str());
                        popFontSafe(fonts_.monoRegular);

                        if (!err.hint.empty()) {
                            ImGui::TextColored(Palette::AccentAmber, "Hint: %s", err.hint.c_str());
                        }
                        if (!err.callStack.empty()) {
                            std::ostringstream stackOss;
                            stackOss << "Stack Unwind: ";
                            for (std::size_t i = 0; i < err.callStack.size(); ++i) {
                                if (i > 0) stackOss << "  ->  ";
                                stackOss << "#" << i << " " << err.callStack[i].operation
                                         << " [L" << err.callStack[i].span.line
                                         << ":C" << err.callStack[i].span.column << "]";
                            }
                            ImGui::TextColored(Palette::TextSecondary, "%s", stackOss.str().c_str());
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor(2);
                    ImGui::Spacing();
                }

                pushFontSafe(fonts_.monoRegular);
                for (const auto& log : consoleLogs_) {
                    const ImVec4 col = log.isError ? Palette::AccentRose : Palette::TextSecondary;
                    ImGui::TextColored(col, "[%s] %s", log.timestamp.c_str(), log.summary.c_str());
                }
                popFontSafe(fonts_.monoRegular);
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

}  // namespace mathcompiler::gui
