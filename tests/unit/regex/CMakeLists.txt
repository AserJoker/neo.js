# tests/unit/regex/CMakeLists.txt - 正则引擎单测（不依赖 IContext）

add_executable(regex_unit_test
    RegexParserTest.cpp
    RegexVMTest.cpp
)
target_link_libraries(regex_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
target_include_directories(regex_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(regex_unit_test)
