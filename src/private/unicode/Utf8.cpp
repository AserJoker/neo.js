// Utf8.cpp - UTF-8 / WTF-8 码点编解码实现

#include "unicode/Utf8.h"

namespace neo::unicode {

bool decodeCodePoint(std::string_view s, size_t& i, uint32_t& cp) {
    if (i >= s.size()) return false;

    auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) {
        cp = b0;
        ++i;
        return true;
    }

    size_t len = 0;
    uint32_t value = 0;
    uint32_t min = 0;
    if ((b0 & 0xE0) == 0xC0) {
        len = 2;
        value = b0 & 0x1Fu;
        min = 0x80u;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3;
        value = b0 & 0x0Fu;
        min = 0x800u;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4;
        value = b0 & 0x07u;
        min = 0x10000u;
    } else {
        return false;
    }

    if (i + len > s.size()) return false;
    for (size_t k = 1; k < len; ++k) {
        auto b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) return false;
        value = (value << 6) | (b & 0x3Fu);
    }
    if (value < min || value > 0x10FFFFu) return false;

    cp = value;
    i += len;
    return true;
}

void encodeCodePoint(uint32_t cp, std::string& out) {
    if (cp > 0x10FFFFu) cp = 0xFFFDu;
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

}  // namespace neo::unicode
