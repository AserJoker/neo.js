// command_test.cpp - CommandParser 单元测试
//
// 关联 ADR: 028（infra）

#include "infra/command.h"

#include <gtest/gtest.h>

namespace {

using neo::infra::CommandOption;
using neo::infra::CommandParser;
using neo::infra::ErrCode;
using neo::infra::ParsedArgs;
using neo::infra::Result;

// 构造解析器，注册常用选项
CommandParser make_parser() {
    CommandParser p;
    p.add_option({"verbose", "v", false, "verbose output"});
    p.add_option({"output",  "o", true,  "output file"});
    p.add_option({"count",   "n", true,  "repeat count"});
    return p;
}

TEST(CommandParserTest, BooleanFlagLong) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--verbose"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("verbose"));
    EXPECT_EQ(r.value().value("verbose"), "true");
}

TEST(CommandParserTest, BooleanFlagShort) {
    auto p = make_parser();
    const char* argv[] = {"prog", "-v"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("verbose"));
}

TEST(CommandParserTest, OptionWithEqualsValue) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--output=result.txt"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("output"));
    EXPECT_EQ(r.value().value("output"), "result.txt");
}

TEST(CommandParserTest, OptionWithSpaceValue) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--output", "out.txt"};
    auto r = p.parse(3, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().value("output"), "out.txt");
}

TEST(CommandParserTest, ShortOptionWithSpaceValue) {
    auto p = make_parser();
    const char* argv[] = {"prog", "-o", "out.txt"};
    auto r = p.parse(3, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().value("output"), "out.txt");
}

TEST(CommandParserTest, PositionalArgs) {
    auto p = make_parser();
    const char* argv[] = {"prog", "file1.js", "file2.js"};
    auto r = p.parse(3, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().positionals().size(), 2u);
    EXPECT_EQ(r.value().positionals()[0], "file1.js");
    EXPECT_EQ(r.value().positionals()[1], "file2.js");
}

TEST(CommandParserTest, MixedOptionsAndPositionals) {
    auto p = make_parser();
    const char* argv[] = {"prog", "-v", "file.js", "-o", "out.txt", "file2.js"};
    auto r = p.parse(6, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("verbose"));
    EXPECT_EQ(r.value().value("output"), "out.txt");
    EXPECT_EQ(r.value().positionals().size(), 2u);
    EXPECT_EQ(r.value().positionals()[0], "file.js");
    EXPECT_EQ(r.value().positionals()[1], "file2.js");
}

TEST(CommandParserTest, DoubleDashEndsOptions) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--verbose", "--", "--not-a-flag", "-x"};
    auto r = p.parse(5, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("verbose"));
    EXPECT_EQ(r.value().positionals().size(), 2u);
    EXPECT_EQ(r.value().positionals()[0], "--not-a-flag");
    EXPECT_EQ(r.value().positionals()[1], "-x");
}

TEST(CommandParserTest, UnknownOptionReturnsErr) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--unknown"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::InvalidArg);
}

TEST(CommandParserTest, UnknownShortOptionReturnsErr) {
    auto p = make_parser();
    const char* argv[] = {"prog", "-z"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::InvalidArg);
}

TEST(CommandParserTest, MissingValueReturnsErr) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--output"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::InvalidArg);
}

TEST(CommandParserTest, FlagWithEqualsValueReturnsErr) {
    auto p = make_parser();
    const char* argv[] = {"prog", "--verbose=true"};
    auto r = p.parse(2, argv);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::InvalidArg);
}

TEST(CommandParserTest, EmptyArgcReturnsErr) {
    auto p = make_parser();
    const char* argv[] = {"prog"};
    auto r = p.parse(0, argv);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.err(), ErrCode::InvalidArg);
}

TEST(CommandParserTest, OnlyProgramName) {
    auto p = make_parser();
    const char* argv[] = {"prog"};
    auto r = p.parse(1, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().positionals().size(), 0u);
}

TEST(CommandParserTest, HasReturnsFalseForMissing) {
    auto p = make_parser();
    const char* argv[] = {"prog"};
    auto r = p.parse(1, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value().has("verbose"));
    EXPECT_FALSE(r.value().has("output"));
}

TEST(CommandParserTest, MultipleFlags) {
    auto p = make_parser();
    const char* argv[] = {"prog", "-v", "-n", "3"};
    auto r = p.parse(4, argv);
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().has("verbose"));
    EXPECT_EQ(r.value().value("count"), "3");
}

}  // namespace
