// RegexUnicode.cpp - Unicode 属性表查询与简单大小写折叠（ADR-062 R3）

#include "regex/RegexUnicode.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace neo::regex {

namespace {

#include "regex/RegexUnicodeTables.inc"

constexpr std::size_t kPropertyCount =
    sizeof(kProperties) / sizeof(kProperties[0]);
constexpr std::size_t kSimpleFoldCount =
    sizeof(kSimpleFold) / sizeof(kSimpleFold[0]);
constexpr std::size_t kFoldOrbitCount =
    sizeof(kFoldOrbit) / sizeof(kFoldOrbit[0]);

const PropertyEntry* findEntry(std::string_view name) {
    for (std::size_t i = 0; i < kPropertyCount; ++i) {
        if (name == kProperties[i].name) return &kProperties[i];
    }
    return nullptr;
}

// ECMAScript 允许的 binary property 白名单（ES2024 Table 67）。
bool isAllowedBinaryProperty(std::string_view name) {
    static constexpr const char* kAllowed[] = {
        "ASCII", "ASCII_Hex_Digit", "Alphabetic", "Any", "Assigned",
        "Bidi_Control", "Bidi_Mirrored", "Case_Ignorable", "Cased",
        "Changes_When_Casefolded", "Changes_When_Casemapped",
        "Changes_When_Lowercased", "Changes_When_NFKC_Casefolded",
        "Changes_When_Titlecased", "Changes_When_Uppercased", "Dash",
        "Default_Ignorable_Code_Point", "Deprecated", "Diacritic", "Emoji",
        "Emoji_Component", "Emoji_Modifier", "Emoji_Modifier_Base",
        "Emoji_Presentation", "Extended_Pictographic", "Extender",
        "Grapheme_Base", "Grapheme_Extend", "Hex_Digit",
        "IDS_Binary_Operator", "IDS_Trinary_Operator", "ID_Continue",
        "ID_Start", "Ideographic", "Join_Control", "Logical_Order_Exception",
        "Lowercase", "Math", "Noncharacter_Code_Point", "Pattern_Syntax",
        "Pattern_White_Space", "Quotation_Mark", "Radical",
        "Regional_Indicator", "Sentence_Terminal", "Soft_Dotted",
        "Terminal_Punctuation", "Unified_Ideograph", "Uppercase",
        "Variation_Selector", "White_Space", "XID_Continue", "XID_Start",
    };
    for (const char* allowed : kAllowed) {
        if (name == allowed) return true;
    }
    return false;
}

bool lookup(std::string_view key, UnicodeProperty& out) {
    const PropertyEntry* entry = findEntry(key);
    if (entry == nullptr) return false;
    out.ranges = entry->ranges;
    out.count = entry->count;
    return true;
}

std::string prefixed(std::string_view prefix, std::string_view value) {
    std::string key;
    key.reserve(prefix.size() + value.size());
    key.append(prefix);
    key.append(value);
    return key;
}

}  // namespace

bool resolveUnicodeProperty(std::string_view name, UnicodeProperty& out) {
    if (isAllowedBinaryProperty(name) && lookup(name, out)) return true;
    return lookup(prefixed("gc:", name), out);
}

bool resolveUnicodeProperty(std::string_view property,
                            std::string_view value, UnicodeProperty& out) {
    if (property == "General_Category" || property == "gc") {
        return lookup(prefixed("gc:", value), out);
    }
    if (property == "Script" || property == "sc") {
        return lookup(prefixed("sc:", value), out);
    }
    if (property == "Script_Extensions" || property == "scx") {
        return lookup(prefixed("scx:", value), out);
    }
    return false;
}

std::uint32_t simpleFold(std::uint32_t codePoint) {
    std::size_t low = 0;
    std::size_t high = kSimpleFoldCount;
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        if (kSimpleFold[mid].from < codePoint) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low < kSimpleFoldCount && kSimpleFold[low].from == codePoint) {
        return kSimpleFold[low].to;
    }
    return codePoint;
}

std::uint32_t simpleUpper(std::uint32_t codePoint) {
    static const std::unordered_map<std::uint32_t, std::uint32_t> upper = [] {
        std::unordered_map<std::uint32_t, std::uint32_t> map;
        for (std::size_t i = 0; i < kSimpleFoldCount; ++i) {
            const std::uint32_t from = kSimpleFold[i].from;
            const std::uint32_t to = kSimpleFold[i].to;
            if (from == to) continue;
            auto it = map.find(to);
            if (it == map.end() || from < it->second) map[to] = from;
        }
        return map;
    }();
    auto it = upper.find(codePoint);
    return it == upper.end() ? codePoint : it->second;
}

const FoldPair* foldOrbitBegin(std::uint32_t codePoint) {
    std::size_t low = 0;
    std::size_t high = kFoldOrbitCount;
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        if (kFoldOrbit[mid].from < codePoint) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return kFoldOrbit + low;
}

const FoldPair* foldOrbitEnd(std::uint32_t codePoint) {
    const FoldPair* begin = foldOrbitBegin(codePoint);
    const FoldPair* end = begin;
    while (end != kFoldOrbit + kFoldOrbitCount && end->from == codePoint) {
        ++end;
    }
    return end;
}

}  // namespace neo::regex
