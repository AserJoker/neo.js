# tests/unit/core/CMakeLists.txt - core 模块单测

add_executable(core_unit_test
    hello_test.cpp
    value_test.cpp
    gc_test.cpp
    scope_test.cpp
    type_test.cpp
    context_test.cpp
)
target_link_libraries(core_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 测试需访问 core 的 private 实现头（MarkSweepGC 等）
target_include_directories(core_unit_test PRIVATE ${CMAKE_SOURCE_DIR}/src/private)

include(GoogleTest)
gtest_discover_tests(core_unit_test)
