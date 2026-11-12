# tests/unit/bigint/CMakeLists.txt - 纯 C++ BigInt 单测

add_executable(bigint_unit_test
    BigIntTest.cpp
)
target_link_libraries(bigint_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
target_include_directories(bigint_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(bigint_unit_test)
