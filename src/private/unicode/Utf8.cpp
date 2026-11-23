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

namespace {

// 判断 3 字节序列是否为代理码点的 WTF-8 编码；是则返回码点。
bool decodeSurrogate3(const char* p, bool high, uint32_t& cp) {
    const auto b0 = static_cast<unsigned char>(p[0]);
    const auto b1 = static_cast<unsigned char>(p[1]);
    const auto b2 = static_cast<unsigned char>(p[2]);
    if (b0 != 0xED || b2 < 0x80 || b2 > 0xBF) return false;
    if (high) {
        if (b1 < 0xA0 || b1 > 0xAF) return false;
        cp = 0xD800u + ((static_cast<uint32_t>(b1 - 0xA0) << 6) |
                        static_cast<uint32_t>(b2 - 0x80));
    } else {
        if (b1 < 0xB0 || b1 > 0xBF) return false;
        cp = 0xDC00u + ((static_cast<uint32_t>(b1 - 0xB0) << 6) |
                        static_cast<uint32_t>(b2 - 0x80));
    }
    return true;
}

}  // namespace

void appendCanonical(std::string& out, std::string_view text) {
    size_t skip = 0;
    uint32_t high = 0;
    uint32_t low = 0;
    if (out.size() >= 3 && text.size() >= 3 &&
        decodeSurrogate3(out.data() + out.size() - 3, true, high) &&
        decodeSurrogate3(text.data(), false, low)) {
        out.resize(out.size() - 3);
        encodeCodePoint(combineSurrogates(high, low), out);
        skip = 3;
    }
    out.append(text.substr(skip));
}

}  // namespace neo::unicode
