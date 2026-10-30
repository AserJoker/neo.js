cmake_minimum_required(VERSION 3.14)
project(neojs C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Compiler warnings
#
# Applied to project targets only — third_party (ICU, GoogleTest) keeps its
# own flags. MSVC-style drivers (cl / clang-cl) get /W4, GNU-style drivers
# get -Wall -Wextra -Wpedantic.
# ---------------------------------------------------------------------------
function(neojs_target_warnings target)
  # The engine deliberately uses the portable C/C++ API instead of MSVC-only
  # *_s variants, so silence that family of CRT diagnostics.
  if(WIN32)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# ICU: static libraries (icuuc/icudata/icui18n/icuio) with embedded data,
# fetched via FetchContent from the ICU GitHub release (offline archives can be
# dropped into third_party/icu/download/). Exposes ICU::uc ICU::i18n ICU::data
# ICU::io alias targets. Consumer must call icu_data_init() once at startup.
add_subdirectory(third_party/icu)

# ---------------------------------------------------------------------------
# Core library: the JavaScript engine runtime (M0: value representation)
# ---------------------------------------------------------------------------
file(GLOB NEOJS_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vm/*.cc")
add_library(neojs_core STATIC ${NEOJS_CORE_SOURCES})
target_include_directories(neojs_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(neojs_core PUBLIC ICU::uc ICU::i18n)
neojs_target_warnings(neojs_core)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(neojs src/main.cc)
target_include_directories(neojs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(neojs PRIVATE neojs_core icu_data_init ICU::data)
neojs_target_warnings(neojs)

# ---------------------------------------------------------------------------
# Unit tests (GoogleTest, git submodule at third_party/gtest)
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build the unit test suite" ON)
if(BUILD_TESTS)
  enable_testing()

  # Force gtest to use the same (dynamic) MSVC CRT as the rest of the project,
  # otherwise linking the test executable fails with a RuntimeLibrary mismatch
  # (MDd vs MTd) under clang-cl / MSVC. Harmless on Linux (no CRT distinction).
  set(gtest_force_shared_crt ON CACHE BOOL "Use shared (DLL) CRT for gtest" FORCE)
  add_subdirectory(third_party/gtest)

  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc")

  add_executable(neojs_test ${TEST_SOURCES})
  target_include_directories(neojs_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(neojs_test PRIVATE neojs_core icu_data_init ICU::data GTest::gtest)
  neojs_target_warnings(neojs_test)

  include(GoogleTest)
  gtest_discover_tests(neojs_test)
endif()
