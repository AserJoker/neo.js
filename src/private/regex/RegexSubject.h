// RegexSubject.h - 匹配主题的 UTF-16 码元视图（ADR-062）
//
// ECMAScript 正则按 UTF-16 code unit 定义语义：非 u 模式下 `/./` 可以匹配
// 孤立代理的一半；u/v 模式按码点推进。Subject 由调用方构造并持有，引擎只借用。
//
// ASCII 快速路径借用 UTF-8 字节（零拷贝）；非 ASCII 借用调用方持有的
// std::u16string。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_SUBJECT_H
#define NEO_REGEX_SUBJECT_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neo::regex {

class Subject {
public:
    Subject() = default;

    // bytes 必须全部是 ASCII（< 0x80）；调用方保证生命周期。
    static Subject fromAscii(std::string_view bytes);
    // units 由调用方持有；引擎只借用。
    static Subject fromUtf16(std::u16string_view units);

    std::size_t size() const {
        return isAscii_ ? ascii_.size() : utf16_.size();
    }
    bool empty() const { return size() == 0; }

    // i < size() 为前置条件。
    char16_t operator[](std::size_t i) const {
        return isAscii_ ? static_cast<char16_t>(
                              static_cast<unsigned char>(ascii_[i]))
                        : utf16_[i];
    }

    // 解码 i 处的一个码点；width 返回占用的码元数（1 或 2）。
    // i < size() 为前置条件。
    std::uint32_t codePointAt(std::size_t i, std::size_t& width) const;

private:
    bool isAscii_ = true;
    std::string_view ascii_;
    std::u16string_view utf16_;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_SUBJECT_H
