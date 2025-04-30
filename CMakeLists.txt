cmake_minimum_required(VERSION 3.12)
project(noix)

set(NOIX_PROJCT_NAME noix)

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.c)

add_executable(${NOIX_PROJCT_NAME} ${SOURCES} ${PROJECT_SOURCE_DIR}/launch/noix.c)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/*.c)

set(SUITS "")

set(TESTS "")

file(GLOB SUIT_PATHS ${PROJECT_SOURCE_DIR}/tests/*)

set(EXTERN_TEST "")
set(IMPORT_TESTS "")

foreach(SUIT_PATH ${SUIT_PATHS})
    if(IS_DIRECTORY ${SUIT_PATH})
        get_filename_component(SUIT ${SUIT_PATH} NAME)
        list(APPEND SUITS ${SUIT})
        file(GLOB TEST_PATHS ${SUIT_PATH}/*.c)

        foreach(TEST_PATH ${TEST_PATHS})
            get_filename_component(TEST ${TEST_PATH} NAME_WE)
            string(APPEND EXTERN_TEST "extern test_item_t TEST_${SUIT}_${TEST}_item;\n")
            string(APPEND IMPORT_TESTS "&TEST_${SUIT}_${TEST}_item,")
            add_test(NAME ${SUIT}.${TEST} COMMAND ${NOIX_PROJCT_NAME}.test --suit ${SUIT} --test ${TEST})
        endforeach(TEST_PATH ${TEST_PATHS})
    endif()
endforeach(SUIT_PATH ${SUIT_PATHS})

string(APPEND IMPORT_TESTS " NULL ")

configure_file(${PROJECT_SOURCE_DIR}/tests/config.h.in ${PROJECT_SOURCE_DIR}/tests/config.h)

add_executable(${NOIX_PROJCT_NAME}.test ${TEST_SOURCES} ${SOURCES})
target_include_directories(${NOIX_PROJCT_NAME}.test PUBLIC ${PROJECT_SOURCE_DIR}/tests)