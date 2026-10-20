#include "neojs/runtime/utf16.h"

#include <unicode/uchar.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>

namespace neojs {

/* UTF-8 串的 UTF-16 code unit 长度：逐码点解码，BMP 内计 1，增补平面计 2。 */
std::size_t utf16Length(const std::string &utf8) {
  std::size_t n = 0;
  std::size_t i = 0;
  const std::size_t len = utf8.size();
  while (i < len) {
    UChar32 c;
    U8_NEXT(utf8.data(), i, static_cast<int32_t>(len), c);
    if (c < 0) {
      ++n; /* 无效字节按 1 code unit 计 */
    } else if (c >= 0x10000) {
      n += 2; /* 增补平面 -> 代理对 */
    } else {
      ++n;
    }
  }
  return n;
}

/* UTF-16 code unit 索引 -> UTF-8 字节偏移。 */
std::size_t utf16IndexToByte(const std::string &utf8, std::size_t i) {
  std::size_t byte = 0;
  std::size_t unit = 0;
  const std::size_t len = utf8.size();
  while (byte < len && unit < i) {
    UChar32 c;
    U8_NEXT(utf8.data(), byte, static_cast<int32_t>(len), c);
    if (c >= 0x10000) {
      unit += 2;
    } else {
      ++unit;
    }
  }
  return byte; /* 到串尾或刚好到目标 code unit 起点 */
}

/* UTF-16 code unit 索引处 code unit 数值。 */
int32_t utf16CharCodeAt(const std::string &utf8, std::size_t i) {
  std::size_t byte = 0;
  std::size_t unit = 0;
  const std::size_t len = utf8.size();
  while (byte < len) {
    UChar32 c;
    U8_NEXT(utf8.data(), byte, static_cast<int32_t>(len), c);
    if (c < 0) c = 0xFFFD;
    if (c >= 0x10000) {
      /* 增补平面：高代理 + 低代理两个 code unit。 */
      if (unit == i) {
        return static_cast<int32_t>(0xD800 + ((c - 0x10000) >> 10));
      }
      if (unit + 1 == i) {
        return static_cast<int32_t>(0xDC00 + ((c - 0x10000) & 0x3FF));
      }
      unit += 2;
    } else {
      if (unit == i) return static_cast<int32_t>(c);
      ++unit;
    }
  }
  return -1; /* 越界 */
}

/* 按 UTF-16 code unit 区间取子串（UTF-8 返回）。 */
std::string utf16Substring(const std::string &utf8, std::size_t from,
                           std::size_t to) {
  const std::size_t len = utf16Length(utf8);
  if (from >= to) return std::string();
  if (from >= len) return std::string();
  if (to > len) to = len;
  const std::size_t b0 = utf16IndexToByte(utf8, from);
  const std::size_t b1 = utf16IndexToByte(utf8, to);
  return utf8.substr(b0, b1 - b0);
}

/* 全串 UTF-8 -> UTF-16（code unit 序列）。 */
std::vector<uint16_t> utf8ToUtf16(const std::string &utf8) {
  std::vector<uint16_t> out;
  out.reserve(utf8.size()); /* 下限：每个字节最多一个 code unit */
  std::size_t i = 0;
  const std::size_t len = utf8.size();
  while (i < len) {
    UChar32 c;
    U8_NEXT(utf8.data(), i, static_cast<int32_t>(len), c);
    if (c < 0) c = 0xFFFD;
    if (c >= 0x10000) {
      uint32_t v = static_cast<uint32_t>(c) - 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (v >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
    } else {
      out.push_back(static_cast<uint16_t>(c));
    }
  }
  return out;
}

/* 全串 UTF-16 -> UTF-8。 */
std::string utf16ToUtf8(const std::vector<uint16_t> &utf16) {
  return utf16ToUtf8(utf16.data(), utf16.size());
}

/* 全串 UTF-16 -> UTF-8（裸指针版）。 */
std::string utf16ToUtf8(const uint16_t *utf16, std::size_t len) {
  std::string out;
  out.reserve(len); /* 下限 */
  std::size_t i = 0;
  while (i < len) {
    uint32_t c = utf16[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && utf16[i + 1] >= 0xDC00 &&
        utf16[i + 1] <= 0xDFFF) {
      /* 代理对：高 + 低。 */
      c = 0x10000 + ((c - 0xD800) << 10) + (utf16[i + 1] - 0xDC00);
      ++i;
    } else if (c >= 0xD800 && c <= 0xDFFF) {
      /* 孤立代理：替换为 U+FFFD（§6.1.4 宽容处理）。 */
      c = 0xFFFD;
    }
    /* 码点 -> UTF-8。 */
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (c >> 18)));
      out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
    ++i;
  }
  return out;
}

/* 在 UTF-16 code unit 数组 [data, data+len) 中从 pos 起查找 search 序列。
 * 返回匹配起始索引；未找到返回 npos。 */
namespace {
std::size_t findUnits(const uint16_t *data, std::size_t len,
                      const uint16_t *needle, std::size_t needleLen,
                      std::size_t pos) {
  if (needleLen == 0) return pos;
  if (needleLen > len || pos > len) return std::string::npos;
  const std::size_t limit = len - needleLen;
  for (std::size_t i = pos; i <= limit; ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needleLen; ++j) {
      if (data[i + j] != needle[j]) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return std::string::npos;
}
}  // namespace

/* 按 UTF-16 code unit 搜索（StringIndexOf）。from 超出串长返回 npos。 */
std::size_t utf16IndexOf(const std::string &utf8, const std::string &search,
                         std::size_t from) {
  std::vector<uint16_t> hay = utf8ToUtf16(utf8);
  std::vector<uint16_t> needle = utf8ToUtf16(search);
  return findUnits(hay.data(), hay.size(), needle.data(), needle.size(), from);
}

/* 按 UTF-16 code unit 反向搜索（StringLastIndexOf）：从 from（含）向左。 */
std::size_t utf16LastIndexOf(const std::string &utf8, const std::string &search,
                             std::size_t from) {
  std::vector<uint16_t> hay = utf8ToUtf16(utf8);
  std::vector<uint16_t> needle = utf8ToUtf16(search);
  if (needle.size() == 0) {
    /* 空 search：返回 min(from, len)（§22.1.3.11 步骤 12 的 StringLastIndexOf
     * 对空串匹配语义：返回 clamp 后的 start）。 */
    return std::min(from, hay.size());
  }
  if (needle.size() > hay.size()) return std::string::npos;
  if (from > hay.size() - needle.size()) from = hay.size() - needle.size();
  for (std::size_t i = from + 1; i-- > 0;) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (hay[i + j] != needle[j]) {
        match = false;
        break;
      }
    }
    if (match) return i;
  }
  return std::string::npos;
}

/* UTF-16 code unit 是否为 trim 空白（TrimString）。 */
bool utf16IsWhiteSpace(uint16_t u) {
  if (u >= 0xD800 && u <= 0xDFFF) return false; /* 代理非空白 */
  return u_isUWhiteSpace(static_cast<UChar32>(u));
}

/* Unicode 大小写映射（root locale Default Case Conversion）。
 * UChar 是 char16_t：用 UChar 向量承载 UTF-16 code unit（与 uint16_t 同宽），
 * 转换前把 uint16_t 结果转成 UChar 数组。 */
namespace {
std::string caseMap(const std::string &utf8, bool upper) {
  std::vector<uint16_t> u16 = utf8ToUtf16(utf8);
  std::vector<UChar> src(u16.begin(), u16.end());
  UErrorCode ec = U_ZERO_ERROR;
  /* 第一遍：只求结果长度。 */
  int32_t len = upper
                    ? u_strToUpper(nullptr, 0, src.data(),
                                   static_cast<int32_t>(src.size()), "", &ec)
                    : u_strToLower(nullptr, 0, src.data(),
                                   static_cast<int32_t>(src.size()), "", &ec);
  if (ec == U_BUFFER_OVERFLOW_ERROR) ec = U_ZERO_ERROR; /* 长度查询预期溢出 */
  if (U_FAILURE(ec)) return utf8; /* ICU 失败：原样返回（宽容） */
  std::vector<UChar> out(static_cast<size_t>(len));
  if (len > 0) {
    ec = U_ZERO_ERROR;
    int32_t n = upper
                    ? u_strToUpper(out.data(), len, src.data(),
                                   static_cast<int32_t>(src.size()), "", &ec)
                    : u_strToLower(out.data(), len, src.data(),
                                   static_cast<int32_t>(src.size()), "", &ec);
    if (U_FAILURE(ec)) return utf8;
    out.resize(static_cast<size_t>(n));
  }
  std::vector<uint16_t> result(out.begin(), out.end());
  return utf16ToUtf8(result.data(), result.size());
}
}  // namespace

std::string utf16ToUpper(const std::string &utf8) { return caseMap(utf8, true); }

std::string utf16ToLower(const std::string &utf8) { return caseMap(utf8, false); }

}  // namespace neojs
