# tests/unit/vm/CMakeLists.txt - VM 模块单测

add_executable(vm_unit_test
    VMTest.cpp
)
target_link_libraries(vm_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 测试需访问 vm 的 private 实现头
target_include_directories(vm_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(vm_unit_test)
