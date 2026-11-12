// unicode/UnicodeClassification.cpp - Unicode 分类谓词实现
//
// ID 表为排序区间数组，二分查找；ASCII 走常量快速路径。
// 空白/行终止符是小集合，直接硬编码（ECMAScript 语义，不是 Unicode 全量）。

#include "unicode/UnicodeClassification.h"

#include <cstddef>
#include <iterator>

namespace neo::unicode {

namespace {

struct CodePointRange {
    uint32_t first;
    uint32_t last;
};

// 由 tools/gen_unicode_id_tables.py 生成（Unicode 15.1.0）。
#include "unicode/UnicodeTables.inc"

bool inRanges(const CodePointRange* ranges, size_t count, uint32_t cp) {
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (cp < ranges[mid].first) {
            high = mid;
        } else if (cp > ranges[mid].last) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

}  // namespace

bool isIdStart(uint32_t cp) {
    if (cp < 0x80u) {
        return (cp >= 0x41u && cp <= 0x5Au) || (cp >= 0x61u && cp <= 0x7Au);
    }
    return inRanges(kIdStartRanges, std::size(kIdStartRanges), cp);
}

bool isIdContinue(uint32_t cp) {
    if (cp < 0x80u) {
        return (cp >= 0x41u && cp <= 0x5Au) || (cp >= 0x61u && cp <= 0x7Au) ||
               (cp >= 0x30u && cp <= 0x39u);
    }
    return inRanges(kIdContinueRanges, std::size(kIdContinueRanges), cp);
}

bool isIdentifierStart(uint32_t cp) {
    return cp == 0x24u /* $ */ || cp == 0x5Fu /* _ */ || isIdStart(cp);
}

bool isIdentifierContinue(uint32_t cp) {
    return cp == 0x24u /* $ */ || cp == 0x5Fu /* _ */ ||
           cp == 0x200Cu /* ZWNJ */ || cp == 0x200Du /* ZWJ */ ||
           isIdContinue(cp);
}

bool isWhitespace(uint32_t cp) {
    switch (cp) {
        case 0x0009u:  // TAB
        case 0x000Bu:  // VT
        case 0x000Cu:  // FF
        case 0x0020u:  // SP
        case 0x00A0u:  // NBSP
        case 0xFEFFu:  // ZWNBSP
            return true;
        default:
            break;
    }
    // Unicode Space_Separator (Zs)。
    if (cp >= 0x2000u && cp <= 0x200Au) return true;
    return cp == 0x1680u || cp == 0x202Fu || cp == 0x205Fu || cp == 0x3000u;
}

bool isLineTerminator(uint32_t cp) {
    return cp == 0x000Au /* LF */ || cp == 0x000Du /* CR */ ||
           cp == 0x2028u /* LS */ || cp == 0x2029u /* PS */;
}

bool isWhitespaceOrLineTerminator(uint32_t cp) {
    return isWhitespace(cp) || isLineTerminator(cp);
}

}  // namespace neo::unicode
