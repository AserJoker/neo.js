// result_test.cpp - Result<T> 单元测试
//
// 关联 ADR: 006（异常即值）、028（infra）

#include "infra/Result.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::infra::ErrCode;
using neo::infra::Result;

TEST(ResultTest, OkHoldsValue) {
    Result<int> r = Result<int>::ok(42);
    ASSERT_TRUE(r.is_ok());
    ASSERT_FALSE(r.is_err());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrHoldsCode) {
    Result<int> r = Result<int>::err(ErrCode::IoError);
    ASSERT_TRUE(r.is_err());
    ASSERT_FALSE(r.is_ok());
    EXPECT_EQ(r.err(), ErrCode::IoError);
}

TEST(ResultTest, OkWithStringMove) {
    Result<std::string> r = Result<std::string>::ok(std::string("hello"));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), "hello");
}

TEST(ResultTest, CopyConstructPreservesOk) {
    Result<int> a = Result<int>::ok(7);
    Result<int> b = a;
    ASSERT_TRUE(b.is_ok());
    EXPECT_EQ(b.value(), 7);
    EXPECT_EQ(a.value(), 7);  // 原对象仍有效
}

TEST(ResultTest, CopyConstructPreservesErr) {
    Result<int> a = Result<int>::err(ErrCode::NotFound);
    Result<int> b = a;
    ASSERT_TRUE(b.is_err());
    EXPECT_EQ(b.err(), ErrCode::NotFound);
}

TEST(ResultTest, MoveConstructTransfersOk) {
    Result<std::string> a = Result<std::string>::ok(std::string("data"));
    Result<std::string> b = std::move(a);
    ASSERT_TRUE(b.is_ok());
    EXPECT_EQ(b.value(), "data");
}

TEST(ResultTest, MoveAssignTransfersOk) {
    Result<std::string> a = Result<std::string>::ok(std::string("first"));
    Result<std::string> b = Result<std::string>::ok(std::string("second"));
    b = std::move(a);
    ASSERT_TRUE(b.is_ok());
    EXPECT_EQ(b.value(), "first");
}

TEST(ResultTest, CopyAssignFromErrToOk) {
    Result<int> a = Result<int>::err(ErrCode::OutOfRange);
    Result<int> b = Result<int>::ok(100);
    b = a;
    ASSERT_TRUE(b.is_err());
    EXPECT_EQ(b.err(), ErrCode::OutOfRange);
}

TEST(ResultVoidTest, OkIsOk) {
    Result<void> r = Result<void>::ok();
    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::Ok);
}

TEST(ResultVoidTest, ErrIsErr) {
    Result<void> r = Result<void>::err(ErrCode::IoError);
    EXPECT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::IoError);
}

}  // namespace
