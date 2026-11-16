// RegexVMTest.cpp - 正则 Compiler + 回溯 VM 端到端单测（ADR-062 R1）

#include "regex/RegexEngine.h"
#include "regex/RegexFlags.h"
#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

using neo::regex::CompileResult;
using neo::regex::ExecOptions;
using neo::regex::MatchResult;
using neo::regex::RegexEngine;
using neo::regex::RegexHost;
using neo::regex::Subject;
using neo::regex::UnicodeTables;

class TestUnicodeTables final : public UnicodeTables {};

class TestHost final : public RegexHost {
public:
    bool abort = false;

    const UnicodeTables& unicode() const override { return tables_; }
    bool shouldAbort(std::uint64_t) override { return abort; }

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

    MatchResult exec(const CompileResult& compiled, std::string_view subject,
                     ExecOptions options = {}) {
        EXPECT_TRUE(compiled.ok())
            << (compiled.error ? compiled.error->message : std::string());
        Subject view = Subject::fromAscii(subject);
        return engine.exec(*compiled.program, view, options);
    }
};

void expectMatch(const MatchResult& result, std::size_t start,
                 std::size_t end) {
    ASSERT_TRUE(result.matched());
    EXPECT_EQ(result.start, start);
    EXPECT_EQ(result.end, end);
}

void expectCapture(const MatchResult& result, std::size_t index,
                   std::size_t start, std::size_t end) {
    ASSERT_LT(index, result.captures.size());
    EXPECT_TRUE(result.captures[index].matched);
    EXPECT_EQ(result.captures[index].start, start);
    EXPECT_EQ(result.captures[index].end, end);
}

void expectCompileError(const CompileResult& result, const char* needle) {
    ASSERT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find(needle), std::string::npos)
        << result.error->message;
}

}  // namespace

TEST(RegexVMTest, MatchesLiteral) {
    Fixture f;
    auto compiled = f.compile("abc");
    expectMatch(f.exec(compiled, "xabcx"), 1, 4);
}

TEST(RegexVMTest, AnchoredSearch) {
    Fixture f;
    auto compiled = f.compile("abc");
    ExecOptions anchored;
    anchored.startIndex = 1;
    anchored.anchored = true;
    expectMatch(f.exec(compiled, "xabcx", anchored), 1, 4);

    anchored.startIndex = 0;
    EXPECT_FALSE(f.exec(compiled, "xabcx", anchored).matched());
}

TEST(RegexVMTest, DotHonorsDotAll) {
    Fixture f;
    auto compiled = f.compile("a.c");
    EXPECT_FALSE(f.exec(compiled, "a\nc").matched());

    auto dotAll = f.compile("a.c", "s");
    expectMatch(f.exec(dotAll, "a\nc"), 0, 3);
}

TEST(RegexVMTest, CharacterClassAndEscapes) {
    Fixture f;
    auto letters = f.compile("[a-c]+");
    expectMatch(f.exec(letters, "xxabc"), 2, 5);

    auto notLetters = f.compile("[^a-c]+");
    expectMatch(f.exec(notLetters, "abcxyz"), 3, 6);

    auto digits = f.compile("\\d+");
    expectMatch(f.exec(digits, "ab123"), 2, 5);

    auto word = f.compile("\\w+");
    expectMatch(f.exec(word, "!a_1 "), 1, 4);

    auto space = f.compile("\\s+");
    expectMatch(f.exec(space, "a \t b"), 1, 4);
}

TEST(RegexVMTest, Quantifiers) {
    Fixture f;
    expectMatch(f.exec(f.compile("a*"), "aaa"), 0, 3);
    expectMatch(f.exec(f.compile("a+?"), "aaa"), 0, 1);
    expectMatch(f.exec(f.compile("a{2,3}"), "aaaa"), 0, 3);
    expectMatch(f.exec(f.compile("a{2,}"), "aaaa"), 0, 4);
    expectMatch(f.exec(f.compile("a{0}"), "aaa"), 0, 0);
}

TEST(RegexVMTest, AlternationIsLeftmostFirst) {
    Fixture f;
    auto compiled = f.compile("a|ab");
    expectMatch(f.exec(compiled, "ab"), 0, 1);

    auto backtracking = f.compile("(a|ab)c");
    MatchResult result = f.exec(backtracking, "abc");
    expectMatch(result, 0, 3);
    expectCapture(result, 1, 0, 2);
}

TEST(RegexVMTest, Captures) {
    Fixture f;
    auto compiled = f.compile("(a)(b)");
    MatchResult result = f.exec(compiled, "ab");
    expectMatch(result, 0, 2);
    expectCapture(result, 0, 0, 2);
    expectCapture(result, 1, 0, 1);
    expectCapture(result, 2, 1, 2);
}

TEST(RegexVMTest, QuantifierKeepsLastCapture) {
    Fixture f;
    auto compiled = f.compile("(a)+");
    MatchResult result = f.exec(compiled, "aaa");
    expectMatch(result, 0, 3);
    expectCapture(result, 1, 2, 3);
}

TEST(RegexVMTest, UnmatchedOptionalCapture) {
    Fixture f;
    auto compiled = f.compile("(a)?b");
    MatchResult result = f.exec(compiled, "b");
    expectMatch(result, 0, 1);
    ASSERT_GT(result.captures.size(), 1u);
    EXPECT_FALSE(result.captures[1].matched);
}

TEST(RegexVMTest, AnchorsAndMultiline) {
    Fixture f;
    auto anchored = f.compile("^abc$");
    EXPECT_TRUE(f.exec(anchored, "abc").matched());
    EXPECT_FALSE(f.exec(anchored, "xabc").matched());

    auto multiline = f.compile("^b", "m");
    expectMatch(f.exec(multiline, "a\nb"), 2, 3);
}

TEST(RegexVMTest, WordBoundaries) {
    Fixture f;
    auto boundary = f.compile("\\bcat\\b");
    expectMatch(f.exec(boundary, "a cat!"), 2, 5);

    auto notBoundary = f.compile("\\Bcat");
    expectMatch(f.exec(notBoundary, "scat"), 1, 4);
}

TEST(RegexVMTest, IgnoreCase) {
    Fixture f;
    auto literal = f.compile("abc", "i");
    expectMatch(f.exec(literal, "ABC"), 0, 3);

    auto range = f.compile("[a-z]+", "i");
    expectMatch(f.exec(range, "ABC"), 0, 3);
}

TEST(RegexVMTest, EmptyQuantifierDoesNotLoopForever) {
    Fixture f;
    auto compiled = f.compile("(a*)*");
    MatchResult result = f.exec(compiled, "b");
    ASSERT_TRUE(result.matched());
    EXPECT_EQ(result.start, 0u);
    EXPECT_EQ(result.end, 0u);

    auto optional = f.compile("(a?)*");
    EXPECT_TRUE(f.exec(optional, "").matched());
}

TEST(RegexVMTest, StepBudgetAborts) {
    Fixture f;
    f.host.abort = true;
    auto compiled = f.compile("a+");
    std::string subject(2000, 'a');
    MatchResult result = f.exec(compiled, subject);
    EXPECT_EQ(result.status, neo::regex::MatchStatus::Aborted);
}

TEST(RegexVMTest, CompilerRejectsUnimplementedNodes) {
    Fixture f;
    expectCompileError(f.compile("(a)\\1"), "backreferences");
    expectCompileError(f.compile("(?=a)"), "lookaround");
    expectCompileError(f.compile("\\p{Letter}", "u"), "property escapes");
}
