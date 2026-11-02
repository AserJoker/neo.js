# tests/unit/infra/CMakeLists.txt - infra 模块单测

add_executable(infra_unit_test
    result_test.cpp
    allocator_test.cpp
    console_test.cpp
    command_test.cpp
    file_test.cpp
)
target_link_libraries(infra_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)

# 临时文件目录（构建目录下 tmp/，不污染系统 temp），编译期宏注入
set(NEO_TEST_TMP_DIR "${CMAKE_BINARY_DIR}/tmp")
file(MAKE_DIRECTORY "${NEO_TEST_TMP_DIR}")
target_compile_definitions(infra_unit_test PRIVATE NEO_TEST_TMP_DIR="${NEO_TEST_TMP_DIR}")

include(GoogleTest)
gtest_discover_tests(infra_unit_test)
