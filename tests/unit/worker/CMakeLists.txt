# tests/unit/worker/CMakeLists.txt - worker 模块单测

add_executable(worker_unit_test
    WorkerTest.cpp
)
target_link_libraries(worker_unit_test
    PRIVATE
        neojs
        neo_warnings
        gtest_main
)
# 测试需访问 core 的 private 实现头（StringData 等）
target_include_directories(worker_unit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src/private
)

include(GoogleTest)
gtest_discover_tests(worker_unit_test)
