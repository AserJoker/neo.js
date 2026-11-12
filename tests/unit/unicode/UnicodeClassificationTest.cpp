// UnicodeClassificationTest.cpp - ID_Start/ID_Continue/空白/行终止符
//
// 覆盖：ASCII 快速路径、非 ASCII ID 属性、ECMAScript 额外字符（$ _ ZWNJ ZWJ）、
// WhiteSpace（含 Zs）与 LineTerminator 的边界。

#include "unicode/UnicodeClassification.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using namespace neo::unicode;

TEST(UnicodeClassificationTest, IdStart) {
    EXPECT_TRUE(isIdStart(0x41u));   // A
    EXPECT_TRUE(isIdStart(0x7Au));   // z
    EXPECT_TRUE(isIdStart(0x00AAu)); // ª
    EXPECT_TRUE(isIdStart(0x00E9u)); // é
    EXPECT_TRUE(isIdStart(0x03B1u)); // α
    EXPECT_TRUE(isIdStart(0x4E2Du)); // 中
    EXPECT_TRUE(isIdStart(0x31350u));  // CJK Ext H 起点

    EXPECT_FALSE(isIdStart(0x30u));    // 0
    EXPECT_FALSE(isIdStart(0x5Fu));    // _
    EXPECT_FALSE(isIdStart(0x24u));    // $
    EXPECT_FALSE(isIdStart(0x00ABu));  // «
    EXPECT_FALSE(isIdStart(0x1F600u)); // emoji
    EXPECT_FALSE(isIdStart(0x200Cu));  // ZWNJ
}

TEST(UnicodeClassificationTest, IdContinue) {
    EXPECT_TRUE(isIdContinue(0x41u));    // A
    EXPECT_TRUE(isIdContinue(0x30u));    // 0
    EXPECT_TRUE(isIdContinue(0x00E9u));  // é
    EXPECT_TRUE(isIdContinue(0x0301u));  // combining acute
    EXPECT_TRUE(isIdContinue(0x00B7u));  // · (Other_ID_Continue)
    EXPECT_TRUE(isIdContinue(0xE0100u)); // variation selector

    EXPECT_FALSE(isIdContinue(0x5Fu));    // _
    EXPECT_FALSE(isIdContinue(0x24u));    // $
    // Unicode 15.1 的 ID_Continue 已包含 ZWNJ/ZWJ（Cf）。
    EXPECT_TRUE(isIdContinue(0x200Cu));   // ZWNJ
    EXPECT_TRUE(isIdContinue(0x200Du));   // ZWJ
    EXPECT_FALSE(isIdContinue(0x1F600u)); // emoji
}

TEST(UnicodeClassificationTest, EcmaIdentifierStartAndPart) {
    EXPECT_TRUE(isIdentifierStart(0x24u));  // $
    EXPECT_TRUE(isIdentifierStart(0x5Fu));  // _
    EXPECT_TRUE(isIdentifierStart(0x41u));  // A
    EXPECT_TRUE(isIdentifierStart(0x4E2Du));  // 中
    EXPECT_FALSE(isIdentifierStart(0x30u)); // 0

    EXPECT_TRUE(isIdentifierContinue(0x24u));   // $
    EXPECT_TRUE(isIdentifierContinue(0x5Fu));   // _
    EXPECT_TRUE(isIdentifierContinue(0x30u));   // 0
    EXPECT_TRUE(isIdentifierContinue(0x200Cu)); // ZWNJ
    EXPECT_TRUE(isIdentifierContinue(0x200Du)); // ZWJ
    EXPECT_FALSE(isIdentifierContinue(0x1F600u));
}

TEST(UnicodeClassificationTest, Whitespace) {
    EXPECT_TRUE(isWhitespace(0x0009u)); // TAB
    EXPECT_TRUE(isWhitespace(0x000Bu)); // VT
    EXPECT_TRUE(isWhitespace(0x000Cu)); // FF
    EXPECT_TRUE(isWhitespace(0x0020u)); // SP
    EXPECT_TRUE(isWhitespace(0x00A0u)); // NBSP
    EXPECT_TRUE(isWhitespace(0xFEFFu)); // ZWNBSP
    EXPECT_TRUE(isWhitespace(0x1680u)); // OGHAM SPACE MARK
    EXPECT_TRUE(isWhitespace(0x2000u)); // EN QUAD
    EXPECT_TRUE(isWhitespace(0x200Au)); // HAIR SPACE
    EXPECT_TRUE(isWhitespace(0x202Fu)); // NARROW NBSP
    EXPECT_TRUE(isWhitespace(0x205Fu)); // MEDIUM MATHEMATICAL SPACE
    EXPECT_TRUE(isWhitespace(0x3000u)); // IDEOGRAPHIC SPACE

    // 行终止符不是 WhiteSpace；NEL/ZWSP 也不是 ECMAScript 空白。
    EXPECT_FALSE(isWhitespace(0x000Au));
    EXPECT_FALSE(isWhitespace(0x000Du));
    EXPECT_FALSE(isWhitespace(0x2028u));
    EXPECT_FALSE(isWhitespace(0x0085u));
    EXPECT_FALSE(isWhitespace(0x200Bu));
    EXPECT_FALSE(isWhitespace(0x41u));
}

TEST(UnicodeClassificationTest, LineTerminator) {
    EXPECT_TRUE(isLineTerminator(0x000Au));  // LF
    EXPECT_TRUE(isLineTerminator(0x000Du));  // CR
    EXPECT_TRUE(isLineTerminator(0x2028u));  // LS
    EXPECT_TRUE(isLineTerminator(0x2029u));  // PS

    EXPECT_FALSE(isLineTerminator(0x000Bu));
    EXPECT_FALSE(isLineTerminator(0x0085u));
    EXPECT_FALSE(isLineTerminator(0x20u));
}

TEST(UnicodeClassificationTest, WhitespaceOrLineTerminator) {
    EXPECT_TRUE(isWhitespaceOrLineTerminator(0x20u));
    EXPECT_TRUE(isWhitespaceOrLineTerminator(0x0Au));
    EXPECT_TRUE(isWhitespaceOrLineTerminator(0x2028u));
    EXPECT_FALSE(isWhitespaceOrLineTerminator(0x41u));
}

}  // namespace
