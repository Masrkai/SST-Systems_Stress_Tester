#? Project Initialization and Compiler Settings
cmake_minimum_required(VERSION 3.19) # bumped from 3.10 -- FetchContent + Slint need >=3.19
project(SystemStressTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # lets clangd (your shell.nix already has clang-tools) see include paths


# a comment to check collaboration ability!









#? Unit Testing Configuration with GoogleTest

# Enable testing
enable_testing()

# Find or fetch GoogleTest
find_package(GTest QUIET)

# Test source files (exclude main.cpp to avoid multiple main() definitions)
set(TEST_SOURCES
    src/ConsoleInitializer.cpp
    src/TimeManager.cpp
    src/CPUStressTest.cpp
    src/MemoryStressTest.cpp
)

# Test files
set(TEST_FILES
    tests/test_cpustresstest.cpp
    tests/test_linkedlist.cpp
    tests/test_memorystresstest.cpp
    # tests/test_timemanager.cpp

    # Add more test files here as you create them
    # tests/test_memorystresstest.cpp
    # tests/test_timemanager.cpp
)

# Create test executable
add_executable(${PROJECT_NAME}_tests ${TEST_SOURCES} ${TEST_FILES})

# Include directories for tests
target_include_directories(${PROJECT_NAME}_tests PRIVATE include)

# Link libraries for tests
target_link_libraries(${PROJECT_NAME}_tests
    gtest_main
    gtest
    Threads::Threads
)










#? Build Configuration, Compiler Flags, and Targets
# Build type handling
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Base compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Build-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -DNDEBUG")
elseif(CMAKE_BUILD_TYPE STREQUAL "Profiling")
    # Profiling build: optimized but with debug info and frame pointers
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g3 -fno-omit-frame-pointer")
    # Optional: preserve more function boundaries for clearer flame graphs
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-inline-small-functions")
    # Add profiling-specific defines
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DPROFILING_BUILD")
endif()

# Platform-specific flags
if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32")
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/ConsoleInitializer.cpp
    src/TimeManager.cpp

    src/CPUStressTest.cpp
    src/MemoryStressTest.cpp
)

# Header files
set(HEADERS
    include/ConsoleInitializer.hpp
    include/ConsoleColors.hpp
    include/LinkedList.hpp
    include/TimeManager.hpp

    include/CPUStressTest.hpp
    include/MemoryStressTest.hpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link threading library
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)




#? Slint GUI Target
# nixpkgs has no ready-made `slint` CMake package (only slint-lsp, the editor
# language server) -- so this pulls Slint's source at configure time and
# builds it via Corrosion. That means shell.nix needs `rustc` + `cargo`
# available (see updated shell.nix), since Slint's C++ API is backed by its
# Rust runtime under the hood.
include(FetchContent)

# Slint auto-detects Qt at configure time and tries to build an optional Qt
# backend if found -- that backend needs OpenGL dev headers (GL/gl.h) that
# aren't available in this shell, and we don't need Qt for this UI anyway
# (the default winit + renderer backend is what main_gui.cpp targets).
# Setting this before FetchContent_MakeAvailable prevents Slint's own
# CMakeLists from ever attempting the Qt backend build.
set(SLINT_FEATURE_BACKEND_QT OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.17.1
    SOURCE_SUBDIR api/cpp
)
FetchContent_MakeAvailable(Slint)

add_executable(sst_gui
    src/main_gui.cpp
    ${TEST_SOURCES}
)
target_include_directories(sst_gui PRIVATE include)
target_link_libraries(sst_gui PRIVATE Threads::Threads Slint::Slint)

# Compiles ui/stress_test_ui.slint into a generated stress_test_ui.h,
# available to main_gui.cpp via #include "stress_test_ui.h"
slint_target_sources(sst_gui ui/stress_test_ui.slint)













# Platform-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} kernel32)
endif()

# Custom targets for different build types
add_custom_target(build-release
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Release -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_SOURCE_DIR}/build
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/build
    COMMENT "Building Release version in build/"
)

add_custom_target(build-profiling
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Profiling -S ${CMAKE_SOURCE_DIR} -B ${CMAKE_SOURCE_DIR}/profiling
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_SOURCE_DIR}/profiling
    COMMENT "Building Profiling version in profiling/"
)