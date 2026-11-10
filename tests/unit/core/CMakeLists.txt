# tests/unit/core/CMakeLists.txt - core 模块单测

add_executable(core_unit_test
    HelloTest.cpp
    ValueTest.cpp
    GcTest.cpp
    GcRootCycleTest.cpp
    ScopeTest.cpp
    TypeTest.cpp
    ContextTest.cpp
    CallStackTest.cpp
    ErrorTest.cpp
    ErrorKindTest.cpp
    InstanceOfTest.cpp
    PromiseTest.cpp
    PromiseChainTest.cpp
    PromiseStaticTest.cpp
    PromiseRejectionTest.cpp
    TypeBehaviorTest.cpp
    DefineStoreTest.cpp
    FunctionTest.cpp
    ObjectTest.cpp
    PropertyOpsTest.cpp
    ConstructTest.cpp
    FunctionConstructibleTest.cpp
    SymbolTest.cpp
    WellKnownSymbolTest.cpp
    AccessorTest.cpp
    ObjectBuiltinsTest.cpp
    ArrayTest.cpp
    ArrayBuiltinsTest.cpp
    IteratorTest.cpp
    IteratorProtocolTest.cpp
    TaskTest.cpp
    TimerTest.cpp
    PrimitiveWrapperTest.cpp
    ToPrimitiveTest.cpp
    InterruptTest.cpp
    CoroutineTest.cpp
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
