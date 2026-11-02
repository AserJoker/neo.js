// hello.cpp - M0 占位实现（验证构建链路）
//
// M0 阶段仅提供 NJS_Version() 占位实现，验证 CMake + Clang + Ninja 构建
// 链路通畅。M1 起本文件被 Value/Context 等实际实现替代。
//
// 关联 ADR: 009（源码结构）、026（M0 项目搭建）

#include "neojs.h"

namespace neo::core {

const char* version() {
    return "neo.js 0.1.0 (M0)";
}

}  // namespace neo::core

const char* NJS_Version(void) {
    return neo::core::version();
}
