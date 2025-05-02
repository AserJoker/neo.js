cmake_minimum_required(VERSION 3.12)
project(noix)

set(CMAKE_CXX_STANDARD 20)

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
