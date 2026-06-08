cmake_minimum_required( VERSION 3.16 )
project( qwc LANGUAGES CXX )


# ---------------------------------------------------------------------------
# Build type
# ---------------------------------------------------------------------------
# Default to an optimized build (the old Makefile always passed -O3).
if( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
  set( CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE )
  set_property( CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel )
endif()

# CMake's stock Release is -O2 -DNDEBUG. Every benchmark in this project was
# measured at -O3, and -O2 vs -O3 can change the autovectorization, so pin it.
# This affects Release and RelWithDebInfo; Debug stays -O0 -g as intended.
set( CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG" )
set( CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -g -DNDEBUG" )

set( CMAKE_CXX_STANDARD 17 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Force scalar countlines for the main `qwc` target (old: `make SCALAR=1`).
option( QWC_SCALAR "Use the portable scalar countlines for qwc" OFF )

# Sanitizers. ASan+UBSan and TSan are mutually exclusive (can't combine in one
# binary), so they're separate switches. Use these in dedicated CLion profiles
# with their own build dirs, never in the benchmark profile.
option( QWC_ASAN "Build with AddressSanitizer + UBSan" OFF )
option( QWC_TSAN "Build with ThreadSanitizer"          OFF )

# Output-name suffix so multiple profiles can coexist without clobbering ./qwc.
# Leave empty for the normal benchmark binary (-> ./qwc). For a sweep, set e.g.
#   -DQWC_SUFFIX=la2   ->   ./qwc-la2
set( QWC_SUFFIX "" CACHE STRING "Suffix for the qwc binary name (e.g. la2 -> qwc-la2)" )

# Generic hook for sweeping compile-time parameters from a profile, mirroring
# how you passed -DADVISE_LOOKAHEAD=N on the command line. Pass a semicolon- or
# space-separated list, e.g.
#   -DQWC_DEFINES=ADVISE_LOOKAHEAD=2;ADVISE_WINDOW_MIB=8
set( QWC_DEFINES "" CACHE STRING "Extra compile definitions (NAME=VALUE list) for sweeps" )

# Build the GoogleTest-based unit tests (under tests/). On by default for the
# top-level build; harmless to leave on for benchmark profiles since the test
# target is only built by `ctest`/explicit `--target qwc_tests`.
option( QWC_BUILD_TESTS "Build unit tests (requires the tests/googletest submodule)" ON )

# ---------------------------------------------------------------------------
# Architecture dispatch (mirrors the Makefile's uname -m logic)
# ---------------------------------------------------------------------------
if( QWC_SCALAR )
  set( QWC_COUNTLINES src/count_scalar.cpp)
  set( QWC_WORDS src/words_scalar.cpp)
  set( QWC_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" )
  set( QWC_COUNTLINES src/count_neon.cpp)
  set( QWC_WORDS src/words_neon.cpp)
  # Bare like the old Makefile: Apple clang targets the native Apple Silicon
  # baseline (NEON included) with no flag. If you ever benchmarked with an
  # explicit -mcpu=apple-mN or -mcpu=native, add it here to match.
  set( QWC_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64" )
  set( QWC_COUNTLINES src/count_avx2.cpp)
  set( QWC_WORDS src/words_scalar.cpp)  # no AVX2 words variant yet
  set( QWC_SIMD_FLAGS -mavx2 )
else()
  set( QWC_COUNTLINES src/count_scalar.cpp)
  set( QWC_WORDS src/words_scalar.cpp)
  set( QWC_SIMD_FLAGS )
endif()
message( STATUS "qwc: building ${QWC_COUNTLINES} and ${QWC_WORDS} for ${CMAKE_SYSTEM_PROCESSOR}" )

# countlines and words are arch-dispatched above (SIMD where available, scalar
# otherwise) and added to each target explicitly. chars and max-line-length are
# scalar-only for now, so they always compile in here.
set( QWC_COMMON_SOURCES src/main.cpp src/cli.cpp src/processfile.cpp src/chars_scalar.cpp src/maxlinelen_scalar.cpp )

# ---------------------------------------------------------------------------
# Shared config applied to a target
# ---------------------------------------------------------------------------
function( qwc_configure target )
  target_include_directories( ${target} PRIVATE include )

  if( QWC_DEFINES )
    target_compile_definitions( ${target} PRIVATE ${QWC_DEFINES} )
  endif()

  if( QWC_ASAN AND QWC_TSAN )
    message( FATAL_ERROR "QWC_ASAN and QWC_TSAN cannot be enabled together." )
  elseif( QWC_ASAN )
    target_compile_options( ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g )
    target_link_options(    ${target} PRIVATE -fsanitize=address,undefined )
  elseif( QWC_TSAN )
    target_compile_options( ${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer -g )
    target_link_options(    ${target} PRIVATE -fsanitize=thread )
  endif()

  # Emit into the project root so benchmark scripts keep finding the binary
  # regardless of which build tree the profile uses.
  set_target_properties( ${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR} )
endfunction()

# ---------------------------------------------------------------------------
# Main target
# ---------------------------------------------------------------------------
if( QWC_SUFFIX STREQUAL "" )
  set( QWC_OUTPUT_NAME "qwc" )
else()
  set( QWC_OUTPUT_NAME "qwc-${QWC_SUFFIX}" )
endif()

add_executable( qwc ${QWC_COMMON_SOURCES} ${QWC_COUNTLINES} ${QWC_WORDS} )
set_target_properties( qwc PROPERTIES OUTPUT_NAME ${QWC_OUTPUT_NAME} )
target_compile_options( qwc PRIVATE ${QWC_SIMD_FLAGS} )
qwc_configure( qwc )
message( STATUS "qwc: output binary -> ${CMAKE_SOURCE_DIR}/${QWC_OUTPUT_NAME}" )

# Portable scalar build, on demand (old: `make qwc-scalar`). Uses the scalar
# implementation of every algorithm -- countlines and words included -- so it
# serves as the baseline the SIMD build is benchmarked against. Built with
# `cmake --build <dir> --target qwc-scalar`; emits ./qwc-scalar.
add_executable( qwc-scalar EXCLUDE_FROM_ALL
  ${QWC_COMMON_SOURCES} src/count_scalar.cpp src/words_scalar.cpp )
qwc_configure( qwc-scalar )
target_compile_options( qwc PRIVATE -Wall -Wextra -Wpedantic )

# ---------------------------------------------------------------------------
# Convenience targets wrapping the Python helpers, run from the project root.
# ---------------------------------------------------------------------------
add_custom_target( gen-test-data
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/gen-test-data.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL )

add_custom_target( bench
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/bench.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS qwc
        USES_TERMINAL )

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if( QWC_BUILD_TESTS )
  enable_testing()
  add_subdirectory( tests )
endif()