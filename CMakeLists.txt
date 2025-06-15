cmake_minimum_required(VERSION 3.12)
project(neo)

set(CMAKE_CXX_STANDARD 20)

set(DerivedCoreProperties "${PROJECT_SOURCE_DIR}/ucd/DerivedCoreProperties.txt")

set(DerivedGeneralCategory "${PROJECT_SOURCE_DIR}/ucd/DerivedGeneralCategory.txt")

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

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

add_executable(${PROJECT_NAME}c ${SOURCES} ${PROJECT_SOURCE_DIR}/cmd/main.c)

add_library(${PROJECT_NAME} ${SOURCES})
