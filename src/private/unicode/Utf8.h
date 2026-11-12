// Utf8.h - UTF-8 / WTF-8 码点编解码（JSON 等需要码点级处理的场景）
//
// 引擎 StringData 用 std::string 存 UTF-8 字节。JSON 的 \uXXXX 与孤立代理
// 需要码点级操作：代理码点按 WTF-8（3 字节）编码，保证 parse/stringify 往返。

#ifndef NEO_UNICODE_UTF8_H
#define NEO_UNICODE_UTF8_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace neo::unicode {

inline bool isHighSurrogate(uint32_t cp) { return cp >= 0xD800 && cp <= 0xDBFF; }
inline bool isLowSurrogate(uint32_t cp) { return cp >= 0xDC00 && cp <= 0xDFFF; }

inline uint32_t combineSurrogates(uint32_t high, uint32_t low) {
    return 0x10000u + ((high - 0xD800u) << 10) + (low - 0xDC00u);
}

// 从 s[i] 解码一个码点并前进 i。接受 WTF-8 代理编码。
// 非法字节返回 false 且不前进（调用方可按字节跳过）。
bool decodeCodePoint(std::string_view s, size_t& i, uint32_t& cp);

// 把码点编码为 UTF-8 / WTF-8（代理码点按 3 字节编码）。
void encodeCodePoint(uint32_t cp, std::string& out);

}  // namespace neo::unicode

#endif  // NEO_UNICODE_UTF8_H
