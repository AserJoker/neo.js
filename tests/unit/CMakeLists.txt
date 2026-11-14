# tests/unit/CMakeLists.txt - 单元测试
#
# 每个模块一个测试可执行，文件命名：<被测对象>_test.cpp。
# 单元测试 mock 外部依赖，隔离测试单函数/单类。

add_subdirectory(core)
add_subdirectory(infra)
add_subdirectory(bigint)
add_subdirectory(frontend)
add_subdirectory(sema)
add_subdirectory(unicode)
add_subdirectory(vm)
add_subdirectory(compiler)
# 以下模块 M1 起填充：
# add_subdirectory(gc)
