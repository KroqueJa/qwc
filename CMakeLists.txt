cmake_minimum_required( VERSION 3.16 )
project( wcl LANGUAGES CXX )

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
# Force scalar countlines for the main `wcl` target (old: `make SCALAR=1`).
option( WCL_SCALAR "Use the portable scalar countlines for wcl" OFF )

# Sanitizers. ASan+UBSan and TSan are mutually exclusive (can't combine in one
# binary), so they're separate switches. Use these in dedicated CLion profiles
# with their own build dirs, never in the benchmark profile.
option( WCL_ASAN "Build with AddressSanitizer + UBSan" OFF )
option( WCL_TSAN "Build with ThreadSanitizer"          OFF )

# Output-name suffix so multiple profiles can coexist without clobbering ./wcl.
# Leave empty for the normal benchmark binary (-> ./wcl). For a sweep, set e.g.
#   -DWCL_SUFFIX=la2   ->   ./wcl-la2
set( WCL_SUFFIX "" CACHE STRING "Suffix for the wcl binary name (e.g. la2 -> wcl-la2)" )

# Generic hook for sweeping compile-time parameters from a profile, mirroring
# how you passed -DADVISE_LOOKAHEAD=N on the command line. Pass a semicolon- or
# space-separated list, e.g.
#   -DWCL_DEFINES=ADVISE_LOOKAHEAD=2;ADVISE_WINDOW_MIB=8
set( WCL_DEFINES "" CACHE STRING "Extra compile definitions (NAME=VALUE list) for sweeps" )

# ---------------------------------------------------------------------------
# Architecture dispatch (mirrors the Makefile's uname -m logic)
# ---------------------------------------------------------------------------
if( WCL_SCALAR )
  set( WCL_COUNTLINES src/countlines_scalar.cpp )
  set( WCL_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" )
  set( WCL_COUNTLINES src/countlines_neon.cpp )
  # Bare like the old Makefile: Apple clang targets the native Apple Silicon
  # baseline (NEON included) with no flag. If you ever benchmarked with an
  # explicit -mcpu=apple-mN or -mcpu=native, add it here to match.
  set( WCL_SIMD_FLAGS )
elseif( CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64" )
  set( WCL_COUNTLINES src/countlines_avx2.cpp )
  set( WCL_SIMD_FLAGS -mavx2 )
else()
  set( WCL_COUNTLINES src/countlines_scalar.cpp )
  set( WCL_SIMD_FLAGS )
endif()
message( STATUS "wcl: building ${WCL_COUNTLINES} for ${CMAKE_SYSTEM_PROCESSOR}" )

set( WCL_COMMON_SOURCES src/main.cpp src/processfile.cpp )

# ---------------------------------------------------------------------------
# Shared config applied to a target
# ---------------------------------------------------------------------------
function( wcl_configure target )
  target_include_directories( ${target} PRIVATE include )

  if( WCL_DEFINES )
    target_compile_definitions( ${target} PRIVATE ${WCL_DEFINES} )
  endif()

  if( WCL_ASAN AND WCL_TSAN )
    message( FATAL_ERROR "WCL_ASAN and WCL_TSAN cannot be enabled together." )
  elseif( WCL_ASAN )
    target_compile_options( ${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g )
    target_link_options(    ${target} PRIVATE -fsanitize=address,undefined )
  elseif( WCL_TSAN )
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
if( WCL_SUFFIX STREQUAL "" )
  set( WCL_OUTPUT_NAME "wcl" )
else()
  set( WCL_OUTPUT_NAME "wcl-${WCL_SUFFIX}" )
endif()

add_executable( wcl ${WCL_COMMON_SOURCES} ${WCL_COUNTLINES} )
set_target_properties( wcl PROPERTIES OUTPUT_NAME ${WCL_OUTPUT_NAME} )
target_compile_options( wcl PRIVATE ${WCL_SIMD_FLAGS} )
wcl_configure( wcl )
message( STATUS "wcl: output binary -> ${CMAKE_SOURCE_DIR}/${WCL_OUTPUT_NAME}" )

# Portable scalar build, on demand (old: `make wcl-scalar`).
add_executable( wcl-scalar EXCLUDE_FROM_ALL ${WCL_COMMON_SOURCES} src/countlines_scalar.cpp )
wcl_configure( wcl-scalar )

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
        DEPENDS wcl
        USES_TERMINAL )