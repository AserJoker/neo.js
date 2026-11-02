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

include(GoogleTest)
gtest_discover_tests(infra_unit_test)
