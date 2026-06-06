# ---------------------------------------------------------------------------
# GoogleTest (vendored as a git submodule under thirdparty/googletest)
# ---------------------------------------------------------------------------
set( WCL_GTEST_DIR "${CMAKE_SOURCE_DIR}/thirdparty/googletest" )
if( NOT EXISTS "${WCL_GTEST_DIR}/CMakeLists.txt" )
  message( FATAL_ERROR
    "GoogleTest submodule is missing. Run:\n"
    "  git submodule update --init --recursive" )
endif()

# We only need googletest, not googlemock, and we don't want it installed.
set( BUILD_GMOCK   OFF CACHE BOOL "" FORCE )
set( INSTALL_GTEST OFF CACHE BOOL "" FORCE )
# On Windows, link gtest against the shared CRT to match consuming targets.
set( gtest_force_shared_crt ON CACHE BOOL "" FORCE )

# Out-of-tree source dir needs an explicit binary dir.
add_subdirectory( ${WCL_GTEST_DIR} ${CMAKE_BINARY_DIR}/thirdparty/googletest EXCLUDE_FROM_ALL )

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------
# Reuse the same countlines implementation (and SIMD flags) the main build
# selected, so we test exactly what ships. WCL_COUNTLINES / WCL_SIMD_FLAGS are
# set by the top-level CMakeLists before this directory is added.
add_executable( wcl_tests
  count_test.cpp
  ${CMAKE_SOURCE_DIR}/${WCL_COUNTLINES}
)
target_include_directories( wcl_tests PRIVATE ${CMAKE_SOURCE_DIR}/include )
target_compile_options( wcl_tests PRIVATE ${WCL_SIMD_FLAGS} )
target_link_libraries( wcl_tests PRIVATE GTest::gtest_main )

include( GoogleTest )
gtest_discover_tests( wcl_tests )
