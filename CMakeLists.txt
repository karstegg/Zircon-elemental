cmake_minimum_required(VERSION 3.22)
project(ZirconElemental VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ZIRCON_BUILD_PLUGIN "Build the JUCE VST3/CLAP/standalone plugin" OFF)

# ---- core engine (no dependencies beyond the bundled single-header JSON)
add_library(zircon_core STATIC
  core/src/ops.cpp
  core/src/compiler.cpp
  core/src/engine.cpp)
target_include_directories(zircon_core PUBLIC core/include third_party)
target_compile_options(zircon_core PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>)

# ---- CLI
add_executable(zircon cli/main.cpp)
target_link_libraries(zircon PRIVATE zircon_core)
target_compile_definitions(zircon PRIVATE ZIRCON_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ---- tests
enable_testing()
add_executable(zircon_tests tests/test_core.cpp)
target_link_libraries(zircon_tests PRIVATE zircon_core)
target_compile_definitions(zircon_tests PRIVATE ZIRCON_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME core COMMAND zircon_tests)

# ---- plugin
if(ZIRCON_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
