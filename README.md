# Scientific Expression Evaluator & Mini-Compiler Suite

An enterprise-grade **C++20** mathematical expression compiler, AST optimizer, stack-based bytecode virtual machine, symbolic differentiation engine, 2D Cartesian function plotter, and **Dear ImGui + OpenGL3** desktop workspace.

---

## Compiler & Execution Architecture

```text
Raw Input
   │
   ▼
[1. Streaming Lexer]       (src/core/Lexer.*)
   │  Dec, Sci (1.5e-3), Hex (0xFF), Bin (0b1010), exact line/start/end cols
   ▼
[2. Shunting-Yard Parser]  (src/core/Parser.*)
   │  Infix ─► RPN + Step Snapshots ─► Typed AST (vars & user functions)
   ▼
[3. AST Optimizer & Diff]  (src/core/AST.*)
   │  Constant Folding, Dead-Code Elimination, Symbolic Differentiation
   ▼
[4. Bytecode Compiler & VM](src/core/BytecodeVM.*, src/core/Evaluator.*)
   │  Custom Stack VM (PUSH, LOAD, STORE, DEF_FUNC, ADD..POW, CALL, HALT)
   │  + AST-Walker parity verification & microsecond profiler
   ▼
[5. 2D Plotter & GUI Suite](src/core/Plotter.*, src/gui/MainWindow.*)
```

### Key Technical Capabilities
- **Lexer (`Lexer.hpp/.cpp`)**: Supports decimals, scientific notation (`1.5e-3`), hexadecimal (`0xFF`), binary (`0b1010`), multi-character identifiers, operators (`+`, `-`, `*`, `/`, `^`, `%`, unary `+/-`, `=`), parentheses, commas, `;`, balanced newlines, and exact `(line, startCol..endCol)` spans.
- **Parser & AST (`Parser.hpp/.cpp`, `AST.hpp/.cpp`)**: Shunting-Yard infix-to-RPN translation building memory-safe `std::unique_ptr<ASTNode>` trees, supporting operator precedence, right-associative `^`/unary ops, user-defined functions (`f(x) = x^2 + 2*x + 1`), constant folding, dead-code/dead-store elimination, and symbolic differentiation (`diff(x^2, x) -> 2 * x`).
- **Stack Bytecode Virtual Machine (`BytecodeVM.hpp/.cpp`)**: Compiles optimized ASTs to stack bytecode (`PUSH`, `LOAD`, `STORE`, `DEF_FUNC`, `ADD`, `SUB`, `MUL`, `DIV`, `POW`, `MOD`, `NEG`, `POS`, `CALL`, `POP`, `HALT`) with recursion-depth guards and stack unwinding.
- **Thread-Safe Symbol Table & Math Library (`SymbolTable.hpp/.cpp`, `MathOps.hpp/.cpp`)**: Guarded by `std::shared_mutex`; pre-seeds `pi`, `e`, `phi`; supports `sin`, `cos`, `tan`, `sqrt`, `exp`, `ln`, `log`, `log10`, `pow`, `abs`, `diff`, and `ndiff`.
- **2D Function Plotter (`Plotter.hpp/.cpp`)**: Compiles expressions once into VM bytecode and samples interactive Cartesian curves over `[xMin, xMax]` with discontinuity/asymptote splitting, zoom/pan, crosshair coordinates, and `f'(x)` derivative overlays.

---

## Workspace Panes
1. **Command & Expression Editor**: Multi-line/single-line syntax-highlighted editor with autocomplete suggestions, presets, history, and optional soft keypad.
2. **Real-Time Result & Execution Profiler**: Symbolic + numeric results (`Auto`, `Fixed`, `Scientific`, `Fraction`), microsecond/nanosecond timings for Lexer, Parser, Optimizer, Bytecode Compiler, Stack VM, and AST-Walker.
3. **2D Function Plotter**: Real-time Cartesian canvas with pan, scroll-zoom, grid ticks, derivative overlay, and crosshair readout.
4. **Memory & Symbol Inspector**: Live editable table for constants, runtime variables, and user-defined functions.
5. **Execution Pipeline & AST Inspector**: Tabbed view of **Token Stream**, **Visual AST Graph & Tree**, **Bytecode VM Disassembly**, **Shunting-Yard (RPN) Step Scrubber**, and **Profiler & Trace**.
6. **Diagnostic Console**: Collapsible error terminal with red `^~~~` caret pointers, hints, and call-stack unwind traces.

---

## Project Layout

```text
├── CMakeLists.txt
├── .clangd
├── README.md
├── src/
│   ├── core/
│   │   ├── SourceLocation.hpp
│   │   ├── CompilerException.hpp / .cpp
│   │   ├── FunctionRegistry.hpp
│   │   ├── Token.hpp
│   │   ├── Lexer.hpp / .cpp
│   │   ├── SymbolTable.hpp / .cpp
│   │   ├── MathOps.hpp / .cpp
│   │   ├── AST.hpp / .cpp
│   │   ├── Parser.hpp / .cpp
│   │   ├── BytecodeVM.hpp / .cpp
│   │   ├── Evaluator.hpp / .cpp
│   │   ├── Plotter.hpp / .cpp
│   │   └── CompilerEngine.hpp / .cpp
│   └── gui/
│       ├── Theme.hpp / .cpp
│       ├── MainWindow.hpp / .cpp
│       └── main.cpp
└── tests/
    └── CompilerTests.cpp
```

---

## Build & Test Instructions

### Prerequisites
- C++20 compiler (MSVC, GCC 11+, or Clang 14+)
- CMake 3.20+
- OpenGL headers/libraries (`opengl32` on Windows, `libgl1-mesa-dev` on Linux)

### Windows (Visual Studio Developer Command Prompt + Ninja)
```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
build\MathCompilerGUI.exe
```

### Linux / macOS
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/MathCompilerGUI
```
