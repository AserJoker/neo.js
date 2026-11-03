// console_test.cpp - Console 单元测试
//
// 验证 Console 格式化输出 + StringSink 截获（可测试性）。
//
// 关联 ADR: 027（FFI 封装/可测试性）、028（infra）

#include "infra/console.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::infra::Console;
using neo::infra::OutputSink;

// 测试用 sink：截获输出到字符串
class StringSink : public OutputSink {
public:
    void write(std::string_view text) override { buffer_ += text; }
    void write_line(std::string_view text) override { buffer_ += text; buffer_ += '\n'; }
    void flush() override {}
    const std::string& buffer() const { return buffer_; }
    std::string take() { return std::move(buffer_); }
private:
    std::string buffer_;
};

TEST(ConsoleTest, WriteRawText) {
    StringSink sink;
    Console c(&sink);
    c.write("hello");
    EXPECT_EQ(sink.buffer(), "hello");
}

TEST(ConsoleTest, WriteLineAppendsNewline) {
    StringSink sink;
    Console c(&sink);
    c.write_line("world");
    EXPECT_EQ(sink.buffer(), "world\n");
}

TEST(ConsoleTest, PrintFormatsArgs) {
    StringSink sink;
    Console c(&sink);
    c.print("val={}, name={}", 42, "neo");
    EXPECT_EQ(sink.buffer(), "val=42, name=neo");
}

TEST(ConsoleTest, PrintLineFormatsAndNewline) {
    StringSink sink;
    Console c(&sink);
    c.print_line("a={} b={}", 1, 2);
    EXPECT_EQ(sink.buffer(), "a=1 b=2\n");
}

TEST(ConsoleTest, MultipleWritesAccumulate) {
    StringSink sink;
    Console c(&sink);
    c.print("part1 ");
    c.print("part2 ");
    c.print_line("part3");
    EXPECT_EQ(sink.buffer(), "part1 part2 part3\n");
}

TEST(ConsoleTest, FormatStringWithVariousTypes) {
    StringSink sink;
    Console c(&sink);
    c.print("int={} str={} float={:.2f} bool={}", 10, "ok", 3.14159, true);
    EXPECT_EQ(sink.buffer(), "int=10 str=ok float=3.14 bool=true");
}

TEST(ConsoleTest, DefaultSinkIsStdoutWhenNullptr) {
    // 不写实际内容到 stdout（避免污染测试输出），仅验证 sink() 非空
    Console c(nullptr);
    EXPECT_NE(c.sink(), nullptr);
}

TEST(StringSinkTest, TakeClearsBuffer) {
    StringSink sink;
    sink.write("abc");
    EXPECT_EQ(sink.take(), "abc");
    EXPECT_EQ(sink.buffer(), "");
}

}  // namespace
