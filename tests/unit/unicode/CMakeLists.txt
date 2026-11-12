# tests/unit/unicode/CMakeLists.txt - Unicode 工具单测

add_executable(unicode_unit_test
    UnicodeClassificationTest.cpp
)
target_link_libraries(unicode_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
target_include_directories(unicode_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(unicode_unit_test)
