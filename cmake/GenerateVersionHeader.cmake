# Build-time version header generator, run in CMake script mode (cmake -P) by
# the qwc_version_header custom target on EVERY build. Resolution chain:
#   1. QWC_VERSION passed in non-empty (-DQWC_VERSION=... on the configure
#      line, forwarded here) -- explicit override for packagers/CI experiments.
#   2. git describe --tags --always --dirty -- the normal case. Exactly
#      "v1.2.3" on a clean tagged checkout (i.e. a release), tag-hash-dirty
#      otherwise, bare commit hash before the first tag exists (--always).
#   3. "local" -- no git, or not a git checkout (e.g. a source tarball).
#
# The header is rewritten ONLY when its content changes, so day-to-day builds
# where the version is stable never recompile cli.cpp or relink.
#
# Inputs (all required, passed with -D):
#   QWC_VERSION  override string, may be empty
#   SOURCE_DIR   the repo root (where git describe should run)
#   OUTPUT_FILE  absolute path of the header to (re)generate

if( NOT QWC_VERSION )
  execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE QWC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE QWC_GIT_RESULT
  )
  if( NOT QWC_GIT_RESULT EQUAL 0 OR QWC_VERSION STREQUAL "" )
    set( QWC_VERSION "local" )
  endif()
endif()

set( QWC_HEADER_CONTENT "#pragma once
#define QWC_VERSION \"${QWC_VERSION}\"
" )

if( EXISTS "${OUTPUT_FILE}" )
  file( READ "${OUTPUT_FILE}" QWC_HEADER_OLD )
else()
  set( QWC_HEADER_OLD "" )
endif()

if( NOT QWC_HEADER_CONTENT STREQUAL QWC_HEADER_OLD )
  file( WRITE "${OUTPUT_FILE}" "${QWC_HEADER_CONTENT}" )
endif()
