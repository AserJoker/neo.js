// hello_main.cpp - M0 HelloWorld 可执行入口
//
// 验证 neojs 静态库链接、C API 调用链路通畅。M1 起被实际宿主/CLI 替代。
//
// 关联 ADR: 026（M0 项目搭建）

#include "neojs.h"

#include <cstdio>
#include <vector>

int main() {
    std::printf("%s\n", NJS_Version());
    return 0;
}
