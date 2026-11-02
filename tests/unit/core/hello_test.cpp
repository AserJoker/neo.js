// hello_test.cpp - M0 GTest 验证用例（M1 起被实际单测替代）
//
// 验证 GTest 引入、neojs 静态库链接、C API 调用。M1 起被各模块单测替代。
//
// 关联 ADR: 007（GTest submodule）、026（M0 项目搭建）、027（测试分层）

#include "neojs.h"

#include <gtest/gtest.h>

#include <string>

TEST(HelloM0, VersionReturnsNeoString) {
    const char* v = NJS_Version();
    ASSERT_NE(v, nullptr);
    std::string s(v);
    EXPECT_NE(s.find("neo.js"), std::string::npos)
        << "version string should contain 'neo.js', got: " << s;
}

TEST(HelloM0, VersionIsStable) {
    const char* a = NJS_Version();
    const char* b = NJS_Version();
    EXPECT_STREQ(a, b);
}
