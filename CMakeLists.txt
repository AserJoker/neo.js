cmake_minimum_required(VERSION 3.12)
project(noix)

set(NOIX_PROJCT_NAME noix)

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)

add_executable(${NOIX_PROJCT_NAME} ${SOURCES})