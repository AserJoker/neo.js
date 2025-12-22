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

if(NOT EXISTS ${DerivedCoreProperties})
    file(DOWNLOAD "https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt" ${DerivedCoreProperties})
endif()

if(NOT EXISTS ${DerivedGeneralCategory})
    file(DOWNLOAD "https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt" ${DerivedGeneralCategory})
endif()

function(get_unicode_attribute file attr output)
    set(ITEMS "")
    file(STRINGS ${file} LINES)

    foreach(LINE ${LINES})
        if(LINE MATCHES "^.*; ${attr}.*$")
            string(REGEX MATCH "([a-z]|[A-Z]|[0-9])+(\.\.([a-z]|[A-Z]|[0-9])+)?" RANGE ${LINE})

            if(RANGE MATCHES "^([a-z]|[A-Z]|[0-9])+$")
                list(APPEND ITEMS "(chr==0x${RANGE})")
            else()
                string(REGEX MATCHALL "([a-z]|[A-Z]|[0-9])+" RANGES ${RANGE})
                list(GET RANGES 0 START)
                list(GET RANGES 1 END)
                list(APPEND ITEMS "(chr>=0x${START}&&chr<=0x${END})")
            endif()
        endif()
    endforeach(LINE ${LINES})

    list(JOIN ITEMS "||" ITEMS)
    set(${output} ${ITEMS} PARENT_SCOPE)
endfunction()

file(STRINGS ${PROJECT_SOURCE_DIR}/ucd/DerivedCoreProperties.txt LINES)

get_unicode_attribute(${DerivedCoreProperties} "ID_Start" ID_STARTS)

get_unicode_attribute(${DerivedCoreProperties} "ID_Continue" ID_CONTINUES)

get_unicode_attribute(${DerivedGeneralCategory} "Zs" SPACE_SEPARATOR)

configure_file(${PROJECT_SOURCE_DIR}/include/core/unicode.gen.h.in ${PROJECT_SOURCE_DIR}/include/core/unicode.gen.h)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/*.cpp)

add_executable(${PROJECT_NAME}c ${SOURCES} ${PROJECT_SOURCE_DIR}/cmd/main.c)

add_executable(${PROJECT_NAME}.test ${SOURCES} ${TEST_SOURCES} ${PROJECT_SOURCE_DIR}/tests/main.cpp)

if(UNIX)
    target_link_libraries(${PROJECT_NAME}c PRIVATE m)
else()
    add_definitions(-D_USE_MATH_DEFINES)
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(pcre2 CONFIG REQUIRED)

find_package(GTest CONFIG REQUIRED)

target_link_libraries(${PROJECT_NAME}c PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX)

target_link_libraries(${PROJECT_NAME} PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX)

target_link_libraries(${PROJECT_NAME}.test PRIVATE PCRE2::8BIT PCRE2::16BIT PCRE2::32BIT PCRE2::POSIX GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)

target_compile_options(${PROJECT_NAME}.test PRIVATE "--coverage")

target_link_options(${PROJECT_NAME}.test PRIVATE "--coverage")

enable_testing()

gtest_discover_tests(${PROJECT_NAME}.test)