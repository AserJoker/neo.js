// CharStreamTest.cpp - UTF-8 只读字符流
//
// 覆盖：空输入、ASCII 步进、peek 前瞻、LF/CRLF/CR/LS/PS 行号、
// 多字节码点的列号与字节偏移、非法 UTF-8。

#include "frontend/CharStream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using neo::frontend::CharStream;

TEST(CharStreamTest, EmptyInput) {
    CharStream stream("");
    EXPECT_TRUE(stream.atEnd());
    EXPECT_EQ(stream.peek(), CharStream::kEndOfInput);
    EXPECT_EQ(stream.advance(), CharStream::kEndOfInput);
    EXPECT_EQ(stream.offset(), 0u);
    EXPECT_EQ(stream.line(), 1u);
    EXPECT_EQ(stream.column(), 1u);
}

TEST(CharStreamTest, AsciiSteppingAndPeek) {
    CharStream stream("abc");
    EXPECT_FALSE(stream.atEnd());
    EXPECT_EQ(stream.peek(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.peek(1), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.peek(2), static_cast<uint32_t>('c'));
    EXPECT_EQ(stream.peek(3), CharStream::kEndOfInput);

    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.offset(), 1u);
    EXPECT_EQ(stream.line(), 1u);
    EXPECT_EQ(stream.column(), 2u);
    EXPECT_EQ(stream.peek(), static_cast<uint32_t>('b'));

    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('c'));
    EXPECT_TRUE(stream.atEnd());
    EXPECT_EQ(stream.advance(), CharStream::kEndOfInput);
    EXPECT_EQ(stream.offset(), 3u);
    EXPECT_EQ(stream.column(), 4u);
}

TEST(CharStreamTest, LineFeed) {
    CharStream stream("a\nb");
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('\n'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 2u);
}

TEST(CharStreamTest, CarriageReturnLineFeedCountsOneLine) {
    CharStream stream("a\r\nb");
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('\r'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('\n'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 2u);
}

TEST(CharStreamTest, CarriageReturnAlone) {
    CharStream stream("a\rb");
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('\r'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 2u);
}

TEST(CharStreamTest, UnicodeLineAndParagraphSeparators) {
    // U+2028 = E2 80 A8，U+2029 = E2 80 A9。
    CharStream stream("a\xE2\x80\xA8" "b\xE2\x80\xA9" "c");
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.advance(), 0x2028u);
    EXPECT_EQ(stream.line(), 2u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('b'));
    EXPECT_EQ(stream.advance(), 0x2029u);
    EXPECT_EQ(stream.line(), 3u);
    EXPECT_EQ(stream.column(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('c'));
    EXPECT_EQ(stream.line(), 3u);
    EXPECT_EQ(stream.column(), 2u);
}

TEST(CharStreamTest, MultibyteCodePointAdvancesColumnByOne) {
    // "é" = C3 A9（2 字节），"中" = E4 B8 AD（3 字节）。
    CharStream stream("\xC3\xA9\xE4\xB8\xAD" "x");
    EXPECT_EQ(stream.peek(), 0x00E9u);
    EXPECT_EQ(stream.advance(), 0x00E9u);
    EXPECT_EQ(stream.offset(), 2u);
    EXPECT_EQ(stream.line(), 1u);
    EXPECT_EQ(stream.column(), 2u);

    EXPECT_EQ(stream.advance(), 0x4E2Du);
    EXPECT_EQ(stream.offset(), 5u);
    EXPECT_EQ(stream.column(), 3u);

    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('x'));
    EXPECT_EQ(stream.offset(), 6u);
    EXPECT_EQ(stream.column(), 4u);
}

TEST(CharStreamTest, PeekLookaheadAcrossMultibyte) {
    CharStream stream("\xC3\xA9x");
    EXPECT_EQ(stream.peek(0), 0x00E9u);
    EXPECT_EQ(stream.peek(1), static_cast<uint32_t>('x'));
    EXPECT_EQ(stream.peek(2), CharStream::kEndOfInput);
    EXPECT_EQ(stream.offset(), 0u);
}

TEST(CharStreamTest, InvalidUtf8ConsumesOneByte) {
    CharStream stream("\xFF" "a");
    EXPECT_EQ(stream.peek(), 0xFFFDu);
    EXPECT_EQ(stream.advance(), 0xFFFDu);
    EXPECT_TRUE(stream.hadInvalidUtf8());
    EXPECT_EQ(stream.offset(), 1u);
    EXPECT_EQ(stream.advance(), static_cast<uint32_t>('a'));
    EXPECT_EQ(stream.offset(), 2u);
}

}  // namespace
