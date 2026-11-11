# tests/integration/core/CMakeLists.txt - core 模块端到端集成测试
#
# 验证 Context + GC + Scope + IType 行为方法全链路协作（真实依赖不 mock）。
# M2 阶段4/5 起填充。

add_executable(core_integration_test
    ArithmeticPipelineTest.cpp
    ArrayPipelineTest.cpp
    IteratorPipelineTest.cpp
    ObjectBuiltinsPipelineTest.cpp
    PrimitiveWrapperPipelineTest.cpp
    TaskPipelineTest.cpp
    ToPrimitivePipelineTest.cpp
)
target_link_libraries(core_integration_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 集成测试需访问 core 的 private 实现头（Scope 构造等）
target_include_directories(core_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(core_integration_test)
