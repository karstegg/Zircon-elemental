cmake_minimum_required(VERSION 3.22)
project(PrimalModular VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PRIMAL_BUILD_PLUGIN "Build the JUCE VST3/CLAP/standalone plugin" OFF)

# ---- core engine (no dependencies beyond the bundled single-header JSON)
add_library(primal_core STATIC
  core/src/ops.cpp
  core/src/compiler.cpp
  core/src/engine.cpp)
target_include_directories(primal_core PUBLIC core/include third_party)
target_compile_options(primal_core PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>)

# ---- CLI
add_executable(primal cli/main.cpp)
target_link_libraries(primal PRIVATE primal_core)
target_compile_definitions(primal PRIVATE PRIMAL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---- tests
enable_testing()
add_executable(primal_tests tests/test_core.cpp)
target_link_libraries(primal_tests PRIVATE primal_core)
target_compile_definitions(primal_tests PRIVATE PRIMAL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME core COMMAND primal_tests)

# ---- plugin
if(PRIMAL_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
