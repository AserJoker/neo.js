# tests/unit/compiler/CMakeLists.txt - Compiler 模块单测

add_executable(compiler_unit_test
    CompilerTest.cpp
)
target_link_libraries(compiler_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 测试需访问 frontend/sema/compiler/vm 的 private 实现头
target_include_directories(compiler_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(compiler_unit_test)
