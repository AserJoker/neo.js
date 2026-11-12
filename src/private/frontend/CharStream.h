// frontend/CharStream.h - 只读 UTF-8 字符流
//
// 数据源是稳定的内存缓冲（std::string_view）：文件读取结果、eval/new Function
// 的字符串都走同一条路径。流不拥有源码，调用方保证缓冲在流生命周期内有效。
//
// 步进按 UTF-8 码点进行：
//   - offset 是字节偏移（用于源码切片）；
//   - line/column 是 1-based 码点位置（用于诊断）；
//   - CRLF 记作一个行终止符；LF / CR / U+2028 / U+2029 各记一行；
//   - 非法 UTF-8 字节按 U+FFFD 消费一个字节，并置 hadInvalidUtf8()。
//
// 不使用 std::stringstream：词法器需要可预测的 O(1) 步进与位置同步。
//
// 关联 ADR: 052（前端源流）

#ifndef NEO_FRONTEND_CHAR_STREAM_H
#define NEO_FRONTEND_CHAR_STREAM_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neo::frontend {

// 游标位置：offset 为字节偏移，line/column 为 1-based 码点位置。
struct SourceLocation {
    size_t offset = 0;
    uint32_t line = 1;
    uint32_t column = 1;
};

// 可保存/恢复的游标状态（供 parser 做推测式 token 前瞻）。
struct CharStreamState {
    size_t offset = 0;
    uint32_t line = 1;
    uint32_t column = 1;
    bool lastWasCR = false;
    bool invalidUtf8 = false;
};

class CharStream {
public:
    // 输入结束哨兵（不是合法 Unicode 码点）。
    static constexpr uint32_t kEndOfInput = 0xFFFFFFFFu;

    explicit CharStream(std::string_view source) : source_(source) {}

    bool atEnd() const { return offset_ >= source_.size(); }

    // 向前看第 lookahead 个码点（0 = 当前）。越过末尾返回 kEndOfInput。
    uint32_t peek(size_t lookahead = 0) const;

    // 消费当前码点并同步位置；末尾返回 kEndOfInput 且不改变状态。
    uint32_t advance();

    size_t offset() const { return offset_; }
    uint32_t line() const { return line_; }
    uint32_t column() const { return column_; }
    SourceLocation location() const { return {offset_, line_, column_}; }

    CharStreamState save() const {
        return {offset_, line_, column_, lastWasCR_, invalidUtf8_};
    }
    void restore(const CharStreamState& state) {
        offset_ = state.offset;
        line_ = state.line;
        column_ = state.column;
        lastWasCR_ = state.lastWasCR;
        invalidUtf8_ = state.invalidUtf8;
    }

    std::string_view source() const { return source_; }

    // 是否在 advance() 中遇到过非法 UTF-8 字节。
    bool hadInvalidUtf8() const { return invalidUtf8_; }

private:
    std::string_view source_;
    size_t offset_ = 0;
    uint32_t line_ = 1;
    uint32_t column_ = 1;
    bool lastWasCR_ = false;   // 让 CRLF 只记一行
    bool invalidUtf8_ = false;
};

}  // namespace neo::frontend

#endif  // NEO_FRONTEND_CHAR_STREAM_H
