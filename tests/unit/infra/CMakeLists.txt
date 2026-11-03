# tests/unit/infra/CMakeLists.txt - infra 模块单测

add_executable(infra_unit_test
    ResultTest.cpp
    AllocatorTest.cpp
    ConsoleTest.cpp
    CommandTest.cpp
)
target_link_libraries(infra_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 测试需访问 infra 的 private 实现头（MockAllocator 委托 MallocAllocator 等）
target_include_directories(infra_unit_test PRIVATE ${CMAKE_SOURCE_DIR}/src/private)

include(GoogleTest)
gtest_discover_tests(infra_unit_test)
