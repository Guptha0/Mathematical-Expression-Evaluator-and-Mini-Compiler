cmake_minimum_required(VERSION 3.20)
project(ScientificExpressionSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_GUI "Build the Dear ImGui desktop application" ON)
option(BUILD_TESTS "Build the Catch2 unit test suite" ON)

include(FetchContent)

add_library(math_compiler_core STATIC
    src/core/CompilerException.cpp
    src/core/Lexer.cpp
    src/core/SymbolTable.cpp
    src/core/MathOps.cpp
    src/core/AST.cpp
    src/core/Parser.cpp
    src/core/BytecodeVM.cpp
    src/core/Evaluator.cpp
    src/core/Plotter.cpp
    src/core/CompilerEngine.cpp
)

target_include_directories(math_compiler_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_features(math_compiler_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(math_compiler_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(math_compiler_core PRIVATE -Wall -Wextra -pedantic)
endif()

if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(math_compiler_tests
        tests/CompilerTests.cpp
    )
    target_link_libraries(math_compiler_tests
        PRIVATE
            math_compiler_core
            Catch2::Catch2WithMain
    )
    if(MSVC)
        target_compile_options(math_compiler_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(math_compiler_tests PRIVATE -Wall -Wextra -pedantic)
    endif()

    add_test(NAME CompilerUnitTests COMMAND math_compiler_tests)
endif()

if(BUILD_GUI)
    find_package(OpenGL REQUIRED)

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b-docking
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(glfw imgui)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(imgui
        PUBLIC
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/backends
    )

    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    add_executable(MathCompilerGUI
        src/gui/Theme.cpp
        src/gui/MainWindow.cpp
        src/gui/main.cpp
    )

    target_include_directories(MathCompilerGUI
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    )

    target_link_libraries(MathCompilerGUI
        PRIVATE
            math_compiler_core
            imgui
            OpenGL::GL
    )

    if(MSVC)
        target_compile_options(MathCompilerGUI PRIVATE /W4 /permissive- /utf-8)
        set_target_properties(MathCompilerGUI PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_options(MathCompilerGUI PRIVATE /ENTRY:mainCRTStartup)
        target_link_libraries(MathCompilerGUI PRIVATE dwmapi)
    elseif(WIN32)
        target_compile_options(MathCompilerGUI PRIVATE -Wall -Wextra -pedantic)
        set_target_properties(MathCompilerGUI PROPERTIES WIN32_EXECUTABLE TRUE)
        target_link_libraries(MathCompilerGUI PRIVATE dwmapi)
    else()
        target_compile_options(MathCompilerGUI PRIVATE -Wall -Wextra -pedantic)
    endif()
endif()
