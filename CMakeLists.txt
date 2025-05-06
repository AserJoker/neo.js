cmake_minimum_required(VERSION 3.12)
project(noix)

set(CMAKE_CXX_STANDARD 20)

file(DOWNLOAD "https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt" "${PROJECT_SOURCE_DIR}/ucd/DerivedCoreProperties.txt")

file(STRINGS ${PROJECT_SOURCE_DIR}/ucd/DerivedCoreProperties.txt LINES)

set(ID_STARTS "")

foreach(LINE ${LINES})
    if(LINE MATCHES "^.*; ID_Start.*$")
        string(REGEX MATCH "([a-z]|[A-Z]|[0-9])+(\.\.([a-z]|[A-Z]|[0-9])+)?" RANGE ${LINE})

        if(RANGE MATCHES "^([a-z]|[A-Z]|[0-9])+$")
            list(APPEND ID_STARTS "(chr==0x${RANGE})")
        else()
            string(REGEX MATCHALL "([a-z]|[A-Z]|[0-9])+" RANGES ${RANGE})
            list(GET RANGES 0 START)
            list(GET RANGES 1 END)
            list(APPEND ID_STARTS "(chr>=0x${START}&&chr<=0x${END})")
        endif()
    endif()
endforeach(LINE ${LINES})

list(JOIN ID_STARTS "||" ID_STARTS)

set(ID_CONTINUES "")

foreach(LINE ${LINES})
    if(LINE MATCHES "^.*; ID_Continue.*$")
        string(REGEX MATCH "([a-z]|[A-Z]|[0-9])+(\.\.([a-z]|[A-Z]|[0-9])+)?" RANGE ${LINE})

        if(RANGE MATCHES "^([a-z]|[A-Z]|[0-9])+$")
            list(APPEND ID_CONTINUES "(chr==0x${RANGE})")
        else()
            string(REGEX MATCHALL "([a-z]|[A-Z]|[0-9])+" RANGES ${RANGE})
            list(GET RANGES 0 START)
            list(GET RANGES 1 END)
            list(APPEND ID_CONTINUES "(chr>=0x${START}&&chr<=0x${END})")
        endif()
    endif()
endforeach(LINE ${LINES})

list(JOIN ID_CONTINUES "||" ID_CONTINUES)

configure_file(${PROJECT_SOURCE_DIR}/include/core/unicode.gen.h.in ${PROJECT_SOURCE_DIR}/include/core/unicode.gen.h)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)

add_executable(${PROJECT_NAME}c ${SOURCES} ${PROJECT_SOURCE_DIR}/cmd/main.c)

add_library(${PROJECT_NAME} ${SOURCES})

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/googletest)

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/*.cpp)

add_executable(${PROJECT_NAME}.test ${TEST_SOURCES} ${SOURCES})

target_link_libraries(${PROJECT_NAME}.test GTest::gtest_main)

enable_testing()

include(GoogleTest)

gtest_discover_tests(${PROJECT_NAME}.test TEST_LISTS gtest)