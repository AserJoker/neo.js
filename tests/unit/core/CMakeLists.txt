# tests/unit/core/CMakeLists.txt - core 模块单测

add_executable(core_unit_test
    hello_test.cpp
)
target_link_libraries(core_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(core_unit_test)
