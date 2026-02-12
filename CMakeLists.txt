cmake_minimum_required(VERSION 3.12)

if(MSVC)
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/execution_charset:utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/source_charset:utf-8>")
endif()

if(WIN32)
    set(VCPKG_TARGET_TRIPLET x64-windows)
else()
    set(VCPKG_TARGET_TRIPLET x64-linux)
endif()

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}\\scripts\\buildsystems\\vcpkg.cmake)

project(neo)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_STANDARD 11)

set(BUILD_STATIC_LIBS on)

set(gtest_force_shared_crt on)

set(DerivedCoreProperties "${PROJECT_SOURCE_DIR}/ucd/DerivedCoreProperties.txt")

set(DerivedGeneralCategory "${PROJECT_SOURCE_DIR}/ucd/DerivedGeneralCategory.txt")

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

include(GoogleTest)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(pcre2 CONFIG REQUIRED)

find_package(ICU REQUIRED COMPONENTS i18n uc data io)

target_link_libraries(${PROJECT_NAME} PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX)

target_link_libraries(${PROJECT_NAME} PRIVATE ICU::i18n ICU::uc ICU::data ICU::io)

if(NEO_ENABLE_TEST)
    add_executable(${PROJECT_NAME}c ${SOURCES} ${PROJECT_SOURCE_DIR}/cmd/main.c)

    if(UNIX)
        target_link_libraries(${PROJECT_NAME}c PRIVATE m)
    else()
        add_definitions(-D_USE_MATH_DEFINES)
    endif()

    target_link_libraries(${PROJECT_NAME}c PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX)

    target_link_libraries(${PROJECT_NAME}c PRIVATE ICU::i18n ICU::uc ICU::data ICU::io)

    file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/*.cpp)

    find_package(GTest CONFIG REQUIRED)

    add_executable(${PROJECT_NAME}.test ${SOURCES} ${TEST_SOURCES} ${PROJECT_SOURCE_DIR}/tests/main.cpp)

    if(UNIX)
        target_link_libraries(${PROJECT_NAME}.test PRIVATE m)
    else()
        add_definitions(-D_USE_MATH_DEFINES)
    endif()

    target_link_libraries(${PROJECT_NAME}.test PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)

    target_link_libraries(${PROJECT_NAME}.test PRIVATE ICU::i18n ICU::uc ICU::data ICU::io)

    enable_testing()

    gtest_discover_tests(${PROJECT_NAME}.test)
endif()