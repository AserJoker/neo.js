# tests/unit/frontend/CMakeLists.txt - 前端模块单测

add_executable(frontend_unit_test
    CharStreamTest.cpp
    LexerTest.cpp
)
target_link_libraries(frontend_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
target_include_directories(frontend_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(frontend_unit_test)
