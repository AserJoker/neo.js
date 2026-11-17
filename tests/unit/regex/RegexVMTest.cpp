// RegexVMTest.cpp - 正则 Compiler + 回溯 VM 端到端单测（ADR-062 R1）

#include "regex/RegexEngine.h"
#include "regex/RegexFlags.h"
#include "regex/RegexHost.h"
#include "regex/RegexMatch.h"
#include "regex/RegexProgram.h"
#include "regex/RegexSubject.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
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

    MatchResult execUtf16(const CompileResult& compiled,
                          std::u16string_view subject,
                          ExecOptions options = {}) {
        EXPECT_TRUE(compiled.ok())
            << (compiled.error ? compiled.error->message : std::string());
        Subject view = Subject::fromUtf16(subject);
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

TEST(RegexVMTest, Backreferences) {
    Fixture f;
    auto literal = f.compile("(a)\\1");
    MatchResult result = f.exec(literal, "aa");
    expectMatch(result, 0, 2);
    expectCapture(result, 1, 0, 1);

    auto alternative = f.compile("(a|b)\\1");
    expectMatch(f.exec(alternative, "bb"), 0, 2);

    auto named = f.compile("(?<x>a)\\k<x>");
    expectMatch(f.exec(named, "aa"), 0, 2);

    auto quantified = f.compile("(a)\\1+");
    expectMatch(f.exec(quantified, "aaa"), 0, 3);

    auto ignoreCase = f.compile("(a)\\1", "i");
    expectMatch(f.exec(ignoreCase, "aA"), 0, 2);
}

TEST(RegexVMTest, BackreferenceToUnmatchedGroupMatchesEmpty) {
    Fixture f;
    auto compiled = f.compile("(a)?\\1b");
    expectMatch(f.exec(compiled, "b"), 0, 1);
}

TEST(RegexVMTest, Lookahead) {
    Fixture f;
    expectMatch(f.exec(f.compile("a(?=b)"), "ab"), 0, 1);
    EXPECT_FALSE(f.exec(f.compile("a(?=b)"), "ac").matched());
    expectMatch(f.exec(f.compile("a(?!b)"), "ac"), 0, 1);
    EXPECT_FALSE(f.exec(f.compile("a(?!b)"), "ab").matched());
    expectMatch(f.exec(f.compile("(?=ab)a"), "ab"), 0, 1);
}

TEST(RegexVMTest, LookaheadCaptureSemantics) {
    Fixture f;
    auto positive = f.compile("(?=(a))a");
    MatchResult kept = f.exec(positive, "a");
    expectMatch(kept, 0, 1);
    expectCapture(kept, 1, 0, 1);

    auto negative = f.compile("(?!(a))b");
    MatchResult discarded = f.exec(negative, "b");
    expectMatch(discarded, 0, 1);
    ASSERT_GT(discarded.captures.size(), 1u);
    EXPECT_FALSE(discarded.captures[1].matched);
}

TEST(RegexVMTest, Lookbehind) {
    Fixture f;
    expectMatch(f.exec(f.compile("(?<=a)b"), "ab"), 1, 2);
    EXPECT_FALSE(f.exec(f.compile("(?<=a)b"), "cb").matched());
    expectMatch(f.exec(f.compile("(?<!a)b"), "cb"), 1, 2);
    EXPECT_FALSE(f.exec(f.compile("(?<!a)b"), "ab").matched());
    expectMatch(f.exec(f.compile("(?<!a)b"), "b"), 0, 1);
}

TEST(RegexVMTest, VariableLengthLookbehind) {
    Fixture f;
    auto plain = f.compile("(?<=a+)b");
    expectMatch(f.exec(plain, "aaab"), 3, 4);

    auto captured = f.compile("(?<=(a+))b");
    MatchResult result = f.exec(captured, "aaab");
    expectMatch(result, 3, 4);
    expectCapture(result, 1, 0, 3);
}

TEST(RegexVMTest, NestedLookaround) {
    Fixture f;
    auto compiled = f.compile("(?=a(?=b))ab");
    expectMatch(f.exec(compiled, "ab"), 0, 2);
}

TEST(RegexVMTest, NamedCaptureIndices) {
    Fixture f;
    auto compiled = f.compile("(?<year>\\d{4})-(?<month>\\d{2})", "d");
    MatchResult result = f.exec(compiled, "2026-11");
    expectMatch(result, 0, 7);
    expectCapture(result, 1, 0, 4);
    expectCapture(result, 2, 5, 7);
}

TEST(RegexVMTest, UnicodeModeMatchesCodePoints) {
    Fixture f;
    const std::u16string emoji = u"\U0001F600";

    auto dot = f.compile(".", "u");
    expectMatch(f.execUtf16(dot, emoji), 0, 2);

    auto literal = f.compile("\\u{1F600}", "u");
    expectMatch(f.execUtf16(literal, emoji), 0, 2);

    auto classLiteral = f.compile("[\\u{1F600}]", "u");
    expectMatch(f.execUtf16(classLiteral, emoji), 0, 2);

    auto range = f.compile("[\\u{1F600}-\\u{1F64F}]", "u");
    expectMatch(f.execUtf16(range, emoji), 0, 2);

    auto emojiProperty = f.compile("\\p{Emoji}", "u");
    expectMatch(f.execUtf16(emojiProperty, emoji), 0, 2);
}

TEST(RegexVMTest, NonUnicodeModeUsesCodeUnits) {
    Fixture f;
    const std::u16string emoji = u"\U0001F600";

    auto dot = f.compile(".");
    expectMatch(f.execUtf16(dot, emoji), 0, 1);

    auto high = f.compile("\\uD83D");
    expectMatch(f.execUtf16(high, emoji), 0, 1);

    auto highUnicode = f.compile("\\uD83D", "u");
    EXPECT_FALSE(f.execUtf16(highUnicode, emoji).matched());
}

TEST(RegexVMTest, UnicodeGeneralCategoryProperties) {
    Fixture f;
    expectMatch(f.exec(f.compile("\\p{Letter}+", "u"), "abc"), 0, 3);
    expectMatch(f.exec(f.compile("\\p{Lu}+", "u"), "ABC"), 0, 3);
    EXPECT_FALSE(f.exec(f.compile("\\p{Lu}+", "u"), "abc").matched());
    expectMatch(f.exec(f.compile("\\p{ASCII}+", "u"), "abc"), 0, 3);
    expectMatch(f.exec(f.compile("\\P{Letter}+", "u"), "123"), 0, 3);
    expectMatch(f.exec(f.compile("\\p{General_Category=Lu}+", "u"), "ABC"),
                0, 3);
}

TEST(RegexVMTest, UnicodeScriptProperties) {
    Fixture f;
    const std::u16string greek = u"\u03B1\u03B2\u03B3";
    expectMatch(f.execUtf16(f.compile("\\p{Script=Greek}+", "u"), greek), 0,
                3);
    expectMatch(f.execUtf16(f.compile("\\p{sc=Grek}+", "u"), greek), 0, 3);
    expectMatch(f.execUtf16(f.compile("\\p{scx=Greek}+", "u"), greek), 0, 3);
}

TEST(RegexVMTest, UnicodePropertyInsideClass) {
    Fixture f;
    auto compiled = f.compile("[\\p{Lu}\\p{Nd}]+", "u");
    expectMatch(f.exec(compiled, "A1"), 0, 2);
    EXPECT_FALSE(f.exec(compiled, "a").matched());
}

TEST(RegexVMTest, UnicodeIgnoreCaseUsesSimpleFolding) {
    Fixture f;
    const std::u16string kelvin = u"\u212A";

    auto k = f.compile("k", "iu");
    expectMatch(f.execUtf16(k, kelvin), 0, 1);

    auto kelvinPattern = f.compile("\\u{212A}", "iu");
    expectMatch(f.exec(kelvinPattern, "k"), 0, 1);

    auto range = f.compile("[a-z]+", "iu");
    expectMatch(f.execUtf16(range, kelvin), 0, 1);

    auto greek = f.compile("\\u{3B1}", "iu");
    expectMatch(f.execUtf16(greek, u"\u0391"), 0, 1);

    auto sharpS = f.compile("\\u{1E9E}", "iu");
    expectMatch(f.execUtf16(sharpS, u"\u00DF"), 0, 1);
}

TEST(RegexVMTest, UnicodeIgnoreCaseWordBoundary) {
    Fixture f;
    auto compiled = f.compile("\\b\\u{212A}\\b", "iu");
    expectMatch(f.execUtf16(compiled, u"\u212A"), 0, 1);
}

TEST(RegexVMTest, NonUnicodeIgnoreCaseStaysAscii) {
    Fixture f;
    auto compiled = f.compile("k", "i");
    EXPECT_FALSE(f.execUtf16(compiled, u"\u212A").matched());
}

TEST(RegexVMTest, CompilerRejectsUnknownProperty) {
    Fixture f;
    expectCompileError(f.compile("\\p{NotAProperty}", "u"),
                       "Invalid property name");
    expectCompileError(f.compile("\\p{Script=NotAScript}", "u"),
                       "Invalid property name");
}

// —— R5：v 模式（Unicode sets） ——

TEST(RegexVMTest, VModeIntersection) {
    Fixture f;
    auto letters = f.compile("[\\p{ASCII}&&\\p{Letter}]+", "v");
    expectMatch(f.exec(letters, "abc"), 0, 3);
    expectMatch(f.exec(letters, "a1"), 0, 1);
}

TEST(RegexVMTest, VModeSubtraction) {
    Fixture f;
    auto consonants = f.compile("[[a-z]--[aeiou]]+", "v");
    expectMatch(f.exec(consonants, "bcdf"), 0, 4);
    EXPECT_FALSE(f.exec(consonants, "ae").matched());
}

TEST(RegexVMTest, VModeNestedNegatedClass) {
    Fixture f;
    auto notA = f.compile("[[a-z]&&[^a]]+", "v");
    expectMatch(f.exec(notA, "bcd"), 0, 3);
    EXPECT_FALSE(f.exec(notA, "a").matched());
}

TEST(RegexVMTest, VModeQStrings) {
    Fixture f;
    auto compiled = f.compile("[\\q{a|b}]+", "v");
    expectMatch(f.exec(compiled, "abba"), 0, 4);
    EXPECT_FALSE(f.exec(compiled, "c").matched());
}

TEST(RegexVMTest, VModeReservedDashRequiresEscape) {
    Fixture f;
    expectCompileError(f.compile("[a-]", "v"), "Invalid character class");
    auto escaped = f.compile("[a\\-]", "v");
    expectMatch(f.exec(escaped, "-"), 0, 1);
}

TEST(RegexVMTest, VModeRejectsMultiCodePointQ) {
    Fixture f;
    expectCompileError(f.compile("[\\q{ab}]", "v"),
                       "properties of strings not implemented");
}

// —— R5：Annex B 兼容 ——

TEST(RegexVMTest, AnnexBIdentityEscapeEightNine) {
    Fixture f;
    expectMatch(f.exec(f.compile("\\8"), "8"), 0, 1);
    expectMatch(f.exec(f.compile("\\9"), "9"), 0, 1);
    expectCompileError(f.compile("\\8", "u"), "Invalid backreference");
}

TEST(RegexVMTest, AnnexBLegacyOctalBackreference) {
    Fixture f;
    expectMatch(f.exec(f.compile("\\1"), std::string_view("\x01", 1)), 0, 1);
    expectMatch(f.exec(f.compile("\\12"), std::string_view("\x0A", 1)), 0, 1);
    // 首位 4-7 只取两位八进制：\118 -> \011 + '8'。
    expectMatch(f.exec(f.compile("\\118"), std::string_view("\x09" "8", 2)),
                0, 2);
}

TEST(RegexVMTest, AnnexBValidBackreferenceStillWins) {
    Fixture f;
    auto compiled = f.compile("(a)\\1");
    expectMatch(f.exec(compiled, "aa"), 0, 2);
}

TEST(RegexVMTest, AnnexBClassControlEscapeFallsBack) {
    Fixture f;
    auto compiled = f.compile("[\\c]");
    expectMatch(f.exec(compiled, "c"), 0, 1);
    expectMatch(f.exec(compiled, "\\"), 0, 1);
}

TEST(RegexVMTest, AnnexBClassEscapeAsRangeEndpoint) {
    Fixture f;
    auto compiled = f.compile("[\\d-a]+");
    expectMatch(f.exec(compiled, "5-a"), 0, 3);
    EXPECT_FALSE(f.exec(compiled, "b").matched());
}

// —— R6：Program 缓存、步数预算与随机压力 ——

TEST(RegexVMTest, CacheReturnsSameProgram) {
    Fixture f;
    auto first = f.compile("a+b");
    auto second = f.compile("a+b");
    ASSERT_TRUE(first.ok());
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(first.program.get(), second.program.get());
    EXPECT_EQ(f.engine.cacheSize(), 1u);

    auto caseInsensitive = f.compile("a+b", "i");
    EXPECT_NE(first.program.get(), caseInsensitive.program.get());
    EXPECT_EQ(f.engine.cacheSize(), 2u);
}

TEST(RegexVMTest, CacheEvictsLeastRecentlyUsed) {
    Fixture f;
    auto first = f.compile("a0");
    ASSERT_TRUE(first.ok());

    for (std::size_t i = 1; i <= RegexEngine::kCacheCapacity; ++i) {
        auto compiled = f.compile("a" + std::to_string(i));
        ASSERT_TRUE(compiled.ok());
    }
    EXPECT_EQ(f.engine.cacheSize(), RegexEngine::kCacheCapacity);

    auto reloaded = f.compile("a0");
    ASSERT_TRUE(reloaded.ok());
    EXPECT_NE(first.program.get(), reloaded.program.get());
}

TEST(RegexVMTest, StepLimitAbortsCatastrophicBacktracking) {
    Fixture f;
    auto compiled = f.compile("(a+)+$");
    ASSERT_TRUE(compiled.ok());

    std::string subject(30, 'a');
    subject.push_back('X');
    ExecOptions options;
    options.stepLimit = 5000;
    MatchResult result = f.exec(compiled, subject, options);
    EXPECT_EQ(result.status, neo::regex::MatchStatus::Aborted);
}

TEST(RegexVMTest, StepLimitAllowsNormalMatch) {
    Fixture f;
    auto compiled = f.compile("a+");
    ExecOptions options;
    options.stepLimit = 100;
    expectMatch(f.exec(compiled, "aaa", options), 0, 3);
}

TEST(RegexVMTest, RandomPatternsDoNotCrash) {
    Fixture f;
    std::mt19937 rng(0xC0FFEEu);
    const std::string patternAlphabet =
        "ab()[]{}|*+?\\^$.-0123456789dDsSwWpPq&";
    const std::string subjectAlphabet = "ab01- \n";

    for (int iteration = 0; iteration < 300; ++iteration) {
        std::string pattern;
        const std::size_t patternLength = 1 + rng() % 12;
        for (std::size_t i = 0; i < patternLength; ++i) {
            pattern.push_back(patternAlphabet[rng() % patternAlphabet.size()]);
        }
        std::string subject;
        const std::size_t subjectLength = rng() % 16;
        for (std::size_t i = 0; i < subjectLength; ++i) {
            subject.push_back(subjectAlphabet[rng() % subjectAlphabet.size()]);
        }

        auto compiled = f.engine.compile(pattern, neo::regex::Flags{});
        if (!compiled.ok()) continue;

        ExecOptions options;
        options.stepLimit = 20000;
        MatchResult result = f.exec(compiled, subject, options);
        EXPECT_TRUE(result.status == neo::regex::MatchStatus::Matched ||
                    result.status == neo::regex::MatchStatus::NoMatch ||
                    result.status == neo::regex::MatchStatus::Aborted ||
                    result.status == neo::regex::MatchStatus::Oom)
            << "pattern=" << pattern;
    }
}
