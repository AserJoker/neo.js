# tests/unit/sema/CMakeLists.txt - Sema 模块单测

add_executable(sema_unit_test
    SemaWalkerTest.cpp
    StrictnessPassTest.cpp
    ScopePassTest.cpp
    ResolvePassTest.cpp
    ControlFlowPassTest.cpp
    ModulePassTest.cpp
    AnnotatePassTest.cpp
    SemaTest.cpp
)
target_link_libraries(sema_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
target_include_directories(sema_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(sema_unit_test)
