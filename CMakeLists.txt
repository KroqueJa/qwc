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

# Opt-in clang-tidy gate. When ON, clang-tidy runs on qwc's own translation
# units during the build (never the vendored googletest) and fails the build on
# any finding. Off by default so normal and benchmark builds stay fast and
# unencumbered. The check list lives in .clang-tidy; CI flips this on.
option( QWC_CLANG_TIDY "Run clang-tidy on qwc sources during the build (gate)" OFF )
if( QWC_CLANG_TIDY )
  find_program( QWC_CLANG_TIDY_EXE NAMES clang-tidy )
  if( NOT QWC_CLANG_TIDY_EXE )
    message( FATAL_ERROR "QWC_CLANG_TIDY=ON but clang-tidy was not found on PATH." )
  endif()
  message( STATUS "qwc: clang-tidy gate enabled (${QWC_CLANG_TIDY_EXE})" )
endif()

# Always emit compile_commands.json so clang-tidy, clangd and other tooling can
# find the exact flags for each translation unit. Costs nothing at build time.
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

# ---------------------------------------------------------------------------
# Architecture dispatch (mirrors the Makefile's uname -m logic)
# ---------------------------------------------------------------------------
if( QWC_SCALAR )
  set( QWC_COUNTLINES src/count_scalar.cpp)
  set( QWC_WORDS src/words_scalar.cpp)
  set( QWC_MAXLINE src/maxlinelen_scalar.cpp)
  set( QWC_CHARS src/chars_scalar.cpp)
  set( QWC_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" )
  set( QWC_COUNTLINES src/count_neon.cpp)
  set( QWC_WORDS src/words_neon.cpp)
  set( QWC_MAXLINE src/maxlinelen_neon.cpp)
  set( QWC_CHARS src/chars_neon.cpp)
  # Bare like the old Makefile: Apple clang targets the native Apple Silicon
  # baseline (NEON included) with no flag. If you ever benchmarked with an
  # explicit -mcpu=apple-mN or -mcpu=native, add it here to match.
  set( QWC_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64" )
  set( QWC_COUNTLINES src/count_avx2.cpp)
  set( QWC_WORDS src/words_avx2.cpp)
  set( QWC_MAXLINE src/maxlinelen_avx2.cpp)
  set( QWC_CHARS src/chars_avx2.cpp)
  # The hand-written kernels use AVX2 (256-bit) intrinsics, so AVX2 is the floor.
  # Default to the x86-64-v3 microarchitecture level -- AVX2 + FMA + BMI1/2 +
  # LZCNT + POPCNT, the standard "modern x86-64" baseline (Haswell / Excavator,
  # ~2015 onward) -- rather than bare -mavx2. It is a strict superset of -mavx2,
  # also sets matching tuning, and lets the compiler autovectorize the scalar
  # frontend TUs (and the count reductions) more aggressively. The trade-off is
  # the minimum CPU: a v3 binary will SIGILL on anything pre-Haswell.
  #
  # Override to benchmark or to build for one specific box, e.g.
  #   -DQWC_X86_ARCH=native      fastest on the build CPU, NOT portable
  #   -DQWC_X86_ARCH=x86-64-v4   AVX-512 (Skylake-X / Zen 4+)
  #   -DQWC_X86_ARCH=x86-64-v2   AVX2-less fallback (SSE4.2 era) -- but note the
  #                              AVX2 intrinsic kernels then need -mavx2 anyway
  # Needs GCC 11+ / Clang 12+ for the x86-64-vN names; use -mavx2 on older tools.
  set( QWC_X86_ARCH "x86-64-v3" CACHE STRING
       "x86-64 -march level for the SIMD qwc build (e.g. x86-64-v3, native)" )
  set( QWC_SIMD_FLAGS -march=${QWC_X86_ARCH} )
else()
  set( QWC_COUNTLINES src/count_scalar.cpp)
  set( QWC_WORDS src/words_scalar.cpp)
  set( QWC_MAXLINE src/maxlinelen_scalar.cpp)
  set( QWC_CHARS src/chars_scalar.cpp)
  set( QWC_SIMD_FLAGS )
endif()
message( STATUS "qwc: building ${QWC_COUNTLINES}, ${QWC_WORDS}, ${QWC_MAXLINE} and ${QWC_CHARS} for ${CMAKE_SYSTEM_PROCESSOR}" )

# Every counting algorithm (countlines, words, max-line-length, chars) is
# arch-dispatched above (SIMD where available, scalar otherwise) and added to
# each target explicitly, so the common set is just the frontend and dispatcher.
set( QWC_COMMON_SOURCES src/main.cpp src/cli.cpp src/processfile.cpp )

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

  # Run the clang-tidy gate on this target's own sources (set per-target, not
  # globally, so the vendored googletest is never linted). --warnings-as-errors
  # makes any finding fail the build.
  if( QWC_CLANG_TIDY AND QWC_CLANG_TIDY_EXE )
    set_target_properties( ${target} PROPERTIES
      CXX_CLANG_TIDY "${QWC_CLANG_TIDY_EXE};--warnings-as-errors=*" )
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

add_executable( qwc ${QWC_COMMON_SOURCES} ${QWC_COUNTLINES} ${QWC_WORDS} ${QWC_MAXLINE} ${QWC_CHARS} )
set_target_properties( qwc PROPERTIES OUTPUT_NAME ${QWC_OUTPUT_NAME} )
target_compile_options( qwc PRIVATE ${QWC_SIMD_FLAGS} )
qwc_configure( qwc )
message( STATUS "qwc: output binary -> ${CMAKE_SOURCE_DIR}/${QWC_OUTPUT_NAME}" )

# Portable scalar build, on demand (old: `make qwc-scalar`). Uses the scalar
# implementation of every algorithm -- countlines, words, max-line-length and
# chars included -- so it serves as the baseline the SIMD build is benchmarked
# against. Built with `cmake --build <dir> --target qwc-scalar`; emits
# ./qwc-scalar.
add_executable( qwc-scalar EXCLUDE_FROM_ALL
  ${QWC_COMMON_SOURCES} src/count_scalar.cpp src/words_scalar.cpp
  src/maxlinelen_scalar.cpp src/chars_scalar.cpp )
qwc_configure( qwc-scalar )
# Build the scalar baseline at the SAME -march as the SIMD qwc (QWC_SIMD_FLAGS).
# This isolates the variable under test -- hand-written SIMD vs the compiler's
# autovectorization -- by giving both binaries the same ISA ceiling (e.g. AVX2
# under x86-64-v3) instead of leaving the baseline at the portable SSE2 default.
target_compile_options( qwc-scalar PRIVATE ${QWC_SIMD_FLAGS} )
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