// unicode/UnicodeCaseNorm.cpp - 全量大小写映射与归一化实现
//
// 数据表为按码点排序的数组，二分查找；Hangul 分解/组合走算法路径。
// 归一化严格按 UAX #15：分解 → 规范排序 → （NFC/NFKC）规范组合。

#include "unicode/UnicodeCaseNorm.h"

#include "unicode/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace neo::unicode {

namespace {

struct UnicodeSimpleMapping {
    uint32_t from;
    uint32_t to;
};

struct UnicodeMapping {
    uint32_t cp;
    const uint32_t* to;
    uint8_t length;
};

struct UnicodeComposition {
    uint32_t first;
    uint32_t second;
    uint32_t composed;
};

struct UnicodeCombiningClass {
    uint32_t cp;
    uint8_t ccc;
};

struct UnicodeRange {
    uint32_t first;
    uint32_t last;
};

// 由 tools/gen_unicode_case_norm_tables.py 生成（Unicode 15.1.0）。
#include "unicode/UnicodeCaseNormTables.inc"

template <typename T, size_t N>
const T* findEntry(const T (&table)[N], uint32_t cp) {
    size_t low = 0;
    size_t high = N;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (table[mid].cp < cp) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < N && table[low].cp == cp) return &table[low];
    return nullptr;
}

template <size_t N>
const UnicodeSimpleMapping* findSimple(const UnicodeSimpleMapping (&table)[N],
                                       uint32_t cp) {
    size_t low = 0;
    size_t high = N;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (table[mid].from < cp) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < N && table[low].from == cp) return &table[low];
    return nullptr;
}

template <size_t N>
bool inRanges(const UnicodeRange (&ranges)[N], uint32_t cp) {
    size_t low = 0;
    size_t high = N;
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

uint32_t simpleLower(uint32_t cp) {
    const UnicodeSimpleMapping* entry = findSimple(kSimpleLower, cp);
    return entry != nullptr ? entry->to : cp;
}

uint32_t simpleUpper(uint32_t cp) {
    const UnicodeSimpleMapping* entry = findSimple(kSimpleUpper, cp);
    return entry != nullptr ? entry->to : cp;
}

uint8_t combiningClass(uint32_t cp) {
    const UnicodeCombiningClass* entry = findEntry(kCombiningClass, cp);
    return entry != nullptr ? entry->ccc : 0;
}

bool isCased(uint32_t cp) { return inRanges(kCasedRanges, cp); }
bool isCaseIgnorable(uint32_t cp) {
    return inRanges(kCaseIgnorableRanges, cp);
}

// Final_Sigma：Σ 前有 Cased（跳过 Case_Ignorable）且后无 Cased。
bool isFinalSigma(const std::vector<uint32_t>& cps, size_t index) {
    size_t before = index;
    while (before > 0) {
        --before;
        if (!isCaseIgnorable(cps[before])) break;
        if (before == 0) return false;
    }
    if (before >= index || !isCased(cps[before])) return false;
    size_t after = index + 1;
    while (after < cps.size() && isCaseIgnorable(cps[after])) ++after;
    return after >= cps.size() || !isCased(cps[after]);
}

std::vector<uint32_t> decode(std::string_view text) {
    std::vector<uint32_t> cps;
    size_t index = 0;
    while (index < text.size()) {
        uint32_t cp = 0;
        if (!decodeCodePoint(text, index, cp)) {
            cps.push_back(static_cast<uint8_t>(text[index]));
            ++index;
            continue;
        }
        cps.push_back(cp);
    }
    return cps;
}

std::string encode(const std::vector<uint32_t>& cps) {
    std::string out;
    out.reserve(cps.size());
    for (const uint32_t cp : cps) encodeCodePoint(cp, out);
    return out;
}

// —— Hangul ——

constexpr uint32_t kSBase = 0xAC00;
constexpr uint32_t kLBase = 0x1100;
constexpr uint32_t kVBase = 0x1161;
constexpr uint32_t kTBase = 0x11A7;
constexpr uint32_t kLCount = 19;
constexpr uint32_t kVCount = 21;
constexpr uint32_t kTCount = 28;
constexpr uint32_t kNCount = kVCount * kTCount;
constexpr uint32_t kSCount = kLCount * kNCount;

bool isHangulSyllable(uint32_t cp) {
    return cp >= kSBase && cp < kSBase + kSCount;
}

void decomposeCodePoint(uint32_t cp, bool compatibility,
                        std::vector<uint32_t>& out) {
    if (isHangulSyllable(cp)) {
        const uint32_t s = cp - kSBase;
        out.push_back(kLBase + s / kNCount);
        out.push_back(kVBase + (s % kNCount) / kTCount);
        const uint32_t t = s % kTCount;
        if (t != 0) out.push_back(kTBase + t);
        return;
    }
    const UnicodeMapping* entry =
        compatibility ? findEntry(kCompatibilityDecomposition, cp)
                      : findEntry(kCanonicalDecomposition, cp);
    if (entry == nullptr) {
        out.push_back(cp);
        return;
    }
    for (uint8_t i = 0; i < entry->length; ++i) {
        decomposeCodePoint(entry->to[i], compatibility, out);
    }
}

// 规范排序：稳定地按 ccc 重排非起始字符。
void canonicalOrder(std::vector<uint32_t>& cps) {
    for (size_t i = 1; i < cps.size(); ++i) {
        const uint8_t ccc = combiningClass(cps[i]);
        if (ccc == 0) continue;
        size_t j = i;
        while (j > 0) {
            const uint8_t prev = combiningClass(cps[j - 1]);
            if (prev <= ccc) break;
            std::swap(cps[j], cps[j - 1]);
            --j;
        }
    }
}

bool composePair(uint32_t first, uint32_t second, uint32_t& composed) {
    // Hangul L + V → LV
    if (first >= kLBase && first < kLBase + kLCount && second >= kVBase &&
        second < kVBase + kVCount) {
        composed = kSBase + ((first - kLBase) * kVCount + (second - kVBase)) *
                                kTCount;
        return true;
    }
    // Hangul LV + T → LVT
    if (isHangulSyllable(first) && (first - kSBase) % kTCount == 0 &&
        second > kTBase && second < kTBase + kTCount) {
        composed = first + (second - kTBase);
        return true;
    }
    size_t low = 0;
    size_t high = std::size(kComposition);
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const UnicodeComposition& entry = kComposition[mid];
        if (entry.first < first ||
            (entry.first == first && entry.second < second)) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < std::size(kComposition) && kComposition[low].first == first &&
        kComposition[low].second == second) {
        composed = kComposition[low].composed;
        return true;
    }
    return false;
}

void compose(std::vector<uint32_t>& cps) {
    std::vector<uint32_t> result;
    result.reserve(cps.size());
    for (const uint32_t cp : cps) {
        if (!result.empty()) {
            // 找最后一个 starter。
            size_t starter = result.size();
            for (size_t i = result.size(); i-- > 0;) {
                if (combiningClass(result[i]) == 0) {
                    starter = i;
                    break;
                }
            }
            if (starter < result.size()) {
                bool blocked = false;
                const uint8_t ccc = combiningClass(cp);
                if (ccc == 0) {
                    blocked = result.size() > starter + 1;
                } else {
                    for (size_t i = starter + 1; i < result.size(); ++i) {
                        if (combiningClass(result[i]) >= ccc) {
                            blocked = true;
                            break;
                        }
                    }
                }
                uint32_t composed = 0;
                if (!blocked && composePair(result[starter], cp, composed)) {
                    result[starter] = composed;
                    continue;
                }
            }
        }
        result.push_back(cp);
    }
    cps.swap(result);
}

}  // namespace

std::string toLowerFull(std::string_view utf8) {
    const std::vector<uint32_t> cps = decode(utf8);
    std::vector<uint32_t> out;
    out.reserve(cps.size());
    for (size_t i = 0; i < cps.size(); ++i) {
        const uint32_t cp = cps[i];
        if (cp == 0x03A3 && isFinalSigma(cps, i)) {
            out.push_back(0x03C2);
            continue;
        }
        const UnicodeMapping* special = findEntry(kLowerSpecial, cp);
        if (special != nullptr) {
            out.insert(out.end(), special->to, special->to + special->length);
        } else {
            out.push_back(simpleLower(cp));
        }
    }
    return encode(out);
}

std::string toUpperFull(std::string_view utf8) {
    const std::vector<uint32_t> cps = decode(utf8);
    std::vector<uint32_t> out;
    out.reserve(cps.size());
    for (const uint32_t cp : cps) {
        const UnicodeMapping* special = findEntry(kUpperSpecial, cp);
        if (special != nullptr) {
            out.insert(out.end(), special->to, special->to + special->length);
        } else {
            out.push_back(simpleUpper(cp));
        }
    }
    return encode(out);
}

bool parseNormalizationForm(std::string_view form, NormalizationForm& out) {
    if (form == "NFC") {
        out = NormalizationForm::NFC;
    } else if (form == "NFD") {
        out = NormalizationForm::NFD;
    } else if (form == "NFKC") {
        out = NormalizationForm::NFKC;
    } else if (form == "NFKD") {
        out = NormalizationForm::NFKD;
    } else {
        return false;
    }
    return true;
}

std::string normalize(std::string_view utf8, NormalizationForm form) {
    const bool compatibility = form == NormalizationForm::NFKC ||
                               form == NormalizationForm::NFKD;
    const bool composeResult = form == NormalizationForm::NFC ||
                               form == NormalizationForm::NFKC;

    std::vector<uint32_t> cps;
    cps.reserve(utf8.size());
    for (const uint32_t cp : decode(utf8)) {
        decomposeCodePoint(cp, compatibility, cps);
    }
    canonicalOrder(cps);
    if (composeResult) compose(cps);
    return encode(cps);
}

}  // namespace neo::unicode
