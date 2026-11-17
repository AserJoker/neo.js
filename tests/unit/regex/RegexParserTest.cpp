// RegexParserTest.cpp - 正则 Parser / flags / Subject 单测（ADR-062 R0）
//
// 引擎单测不依赖 IContext：TestHost 只提供空的 Unicode 表与永不中止策略。

#include "regex/RegexAST.h"
#include "regex/RegexEngine.h"
#include "regex/RegexFlags.h"
#include "regex/RegexHost.h"
#include "regex/RegexParser.h"
#include "regex/RegexSubject.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using neo::regex::CompileResult;
using neo::regex::Flags;
using neo::regex::RegexEngine;
using neo::regex::RegexHost;
using neo::regex::Subject;
using neo::regex::UnicodeTables;

class TestUnicodeTables final : public UnicodeTables {};

class TestHost final : public RegexHost {
public:
    const UnicodeTables& unicode() const override { return tables_; }
    bool shouldAbort(std::uint64_t) override { return false; }

private:
    TestUnicodeTables tables_;
};

struct Fixture {
    TestHost host;
    RegexEngine engine{host};

    CompileResult compile(const std::string& pattern,
                          const std::string& flags = "") {
        auto parsed = neo::regex::parseFlags(flags);
        EXPECT_TRUE(parsed.ok())
            << (parsed.error ? parsed.error->message : std::string());
        return engine.compile(pattern, parsed.flags);
    }

    // 只测语法：不进入 Compiler，允许 R2/R3 才实现的节点。
    neo::regex::ParseResult parse(const std::string& pattern,
                                  const std::string& flags = "") {
        auto parsed = neo::regex::parseFlags(flags);
        EXPECT_TRUE(parsed.ok())
            << (parsed.error ? parsed.error->message : std::string());
        neo::regex::RegexParser parser(pattern, parsed.flags, {});
        return parser.parse();
    }
};

void expectError(const CompileResult& result, const char* needle) {
    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find(needle), std::string::npos)
        << result.error->message;
}

}  // namespace

TEST(RegexFlagsTest, ParsesAllFlags) {
    auto result = neo::regex::parseFlags("dgimsuy");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.flags.hasIndices);
    EXPECT_TRUE(result.flags.global);
    EXPECT_TRUE(result.flags.ignoreCase);
    EXPECT_TRUE(result.flags.multiline);
    EXPECT_TRUE(result.flags.dotAll);
    EXPECT_TRUE(result.flags.unicode);
    EXPECT_FALSE(result.flags.unicodeSets);
    EXPECT_TRUE(result.flags.sticky);
}

TEST(RegexFlagsTest, ParsesUnicodeSets) {
    auto result = neo::regex::parseFlags("v");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.flags.unicodeSets);
    EXPECT_FALSE(result.flags.unicode);
}

TEST(RegexFlagsTest, RejectsDuplicateFlag) {
    auto result = neo::regex::parseFlags("gg");
    EXPECT_FALSE(result.ok());
}

TEST(RegexFlagsTest, RejectsUnknownFlag) {
    auto result = neo::regex::parseFlags("z");
    EXPECT_FALSE(result.ok());
}

TEST(RegexFlagsTest, RejectsUnicodeAndUnicodeSets) {
    auto result = neo::regex::parseFlags("uv");
    EXPECT_FALSE(result.ok());
}

TEST(RegexParserTest, CompilesSimplePattern) {
    Fixture f;
    CompileResult result = f.compile("abc");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.program->source, "abc");
    EXPECT_EQ(result.program->captureCount, 0u);
}

TEST(RegexParserTest, CompilesAlternationWithEmptyAlternative) {
    Fixture f;
    EXPECT_TRUE(f.compile("a|b|").ok());
}

TEST(RegexParserTest, CountsCapturesAndNamedGroups) {
    Fixture f;
    CompileResult result = f.compile("(a)(?:b)(?<name>c)");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.program->captureCount, 2u);
    auto it = result.program->namedGroups.find("name");
    ASSERT_NE(it, result.program->namedGroups.end());
    EXPECT_EQ(it->second, 2u);
}

TEST(RegexParserTest, ParsesQuantifiers) {
    Fixture f;
    EXPECT_TRUE(f.compile("a*b+?c{2}d{2,}e{2,3}?").ok());
}

TEST(RegexParserTest, ParsesCharacterClass) {
    Fixture f;
    EXPECT_TRUE(f.parse("[a-z\\d\\p{Letter}]", "u").ok());
}

TEST(RegexParserTest, ParsesAssertions) {
    Fixture f;
    EXPECT_TRUE(f.parse("^$\\b\\B(?=a)(?!b)(?<=c)(?<!d)").ok());
}

TEST(RegexParserTest, ParsesBackreferences) {
    Fixture f;
    EXPECT_TRUE(f.parse("(a)\\1").ok());
    EXPECT_TRUE(f.parse("(?<x>a)\\k<x>").ok());
}

TEST(RegexParserTest, ParsesUnicodeCodePointEscape) {
    Fixture f;
    EXPECT_TRUE(f.compile("\\u{1F600}", "u").ok());
}

TEST(RegexParserTest, RejectsUnterminatedGroup) {
    Fixture f;
    expectError(f.compile("(a"), "Unterminated group");
}

TEST(RegexParserTest, RejectsUnterminatedCharacterClass) {
    Fixture f;
    expectError(f.compile("[a"), "Unterminated character class");
}

TEST(RegexParserTest, RejectsNothingToRepeat) {
    Fixture f;
    expectError(f.compile("*a"), "Nothing to repeat");
}

TEST(RegexParserTest, RejectsQuantifierOutOfOrder) {
    Fixture f;
    expectError(f.compile("a{3,2}"), "out of order");
}

TEST(RegexParserTest, RejectsDuplicateNamedGroup) {
    Fixture f;
    expectError(f.compile("(?<x>a)(?<x>b)"), "Duplicate capture group name");
}

TEST(RegexParserTest, RejectsUnknownNamedBackreference) {
    Fixture f;
    expectError(f.compile("\\k<x>"), "Invalid named capture");
}

TEST(RegexParserTest, RejectsUnknownBackreference) {
    Fixture f;
    // Annex B 下 \1 退化为 LegacyOctalEscapeSequence；u 模式仍报错。
    expectError(f.compile("\\1", "u"), "Invalid backreference");
}

TEST(RegexParserTest, RejectsInvalidGroup) {
    Fixture f;
    expectError(f.compile("(?a)"), "Invalid group");
}

TEST(RegexParserTest, RejectsOutOfRangeUnicodeEscape) {
    Fixture f;
    expectError(f.compile("\\u{110000}", "u"), "Invalid Unicode escape");
}

TEST(RegexSubjectTest, AsciiView) {
    Subject subject = Subject::fromAscii("abc");
    EXPECT_EQ(subject.size(), 3u);
    EXPECT_EQ(subject[1], u'b');
    std::size_t width = 0;
    EXPECT_EQ(subject.codePointAt(1, width), 0x62u);
    EXPECT_EQ(width, 1u);
}

TEST(RegexSubjectTest, Utf16SurrogatePair) {
    const char16_t units[] = {u'a', 0xD83D, 0xDE00, u'b'};
    Subject subject = Subject::fromUtf16(std::u16string_view(units, 4));
    EXPECT_EQ(subject.size(), 4u);
    EXPECT_EQ(subject[1], 0xD83D);
    std::size_t width = 0;
    EXPECT_EQ(subject.codePointAt(1, width), 0x1F600u);
    EXPECT_EQ(width, 2u);
    EXPECT_EQ(subject.codePointAt(3, width), 0x62u);
    EXPECT_EQ(width, 1u);
}
