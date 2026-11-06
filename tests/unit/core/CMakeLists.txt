# tests/unit/core/CMakeLists.txt - core 模块单测

add_executable(core_unit_test
    HelloTest.cpp
    ValueTest.cpp
    GcTest.cpp
    ScopeTest.cpp
    TypeTest.cpp
    ContextTest.cpp
    TypeBehaviorTest.cpp
    DefineStoreTest.cpp
    FunctionTest.cpp
    ObjectTest.cpp
    PropertyOpsTest.cpp
    ConstructTest.cpp
    SymbolTest.cpp
    WellKnownSymbolTest.cpp
    AccessorTest.cpp
    ObjectBuiltinsTest.cpp
    ArrayTest.cpp
    ArrayBuiltinsTest.cpp
    IteratorTest.cpp
    IteratorProtocolTest.cpp
    PrimitiveWrapperTest.cpp
    ToPrimitiveTest.cpp
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
