# tests/unit/tools/CMakeLists.txt - 内部工具单测

add_executable(tools_unit_test
    Test262FrontmatterTest.cpp
    ${CMAKE_SOURCE_DIR}/tools/test262/Frontmatter.cpp
)

target_link_libraries(tools_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)

target_include_directories(tools_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/test262
)

include(GoogleTest)
gtest_discover_tests(tools_unit_test)
