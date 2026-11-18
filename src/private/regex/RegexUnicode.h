// RegexUnicode.h - 正则引擎的 Unicode 属性与简单大小写折叠（ADR-062 R3）
//
// 数据由 tools/gen_unicode_regex_tables.py 从 Unicode 15.1.0 UCD 生成，
// 零运行时依赖。属性名解析遵循 ECMAScript 的 \p{...} 语法：
//   - 单独值：General_Category 值别名或 binary property 名；
//   - property=value：General_Category/gc、Script/sc、Script_Extensions/scx。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_UNICODE_H
#define NEO_REGEX_UNICODE_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neo::regex {

struct CodePointRange {
    std::uint32_t from = 0;
    std::uint32_t to = 0;
};

struct FoldPair {
    std::uint32_t from = 0;
    std::uint32_t to = 0;
};

struct UnicodeProperty {
    const CodePointRange* ranges = nullptr;
    std::size_t count = 0;

    bool valid() const { return ranges != nullptr; }
};

// 解析 \p{name} 中的 name（不含花括号）。
bool resolveUnicodeProperty(std::string_view name, UnicodeProperty& out);

// 解析 \p{property=value}。
bool resolveUnicodeProperty(std::string_view property,
                            std::string_view value, UnicodeProperty& out);

// CaseFolding.txt 的 C+S 映射；无映射时返回 cp 本身。
std::uint32_t simpleFold(std::uint32_t codePoint);

// simpleFold 的逆映射（取最小原像）；无映射时返回 cp 本身。
// 供 String.prototype.toUpperCase 的简单大小写映射使用。
std::uint32_t simpleUpper(std::uint32_t codePoint);

// cp 的简单折叠等价类（不含 cp 本身）；返回有序区间 [begin, end)。
const FoldPair* foldOrbitBegin(std::uint32_t codePoint);
const FoldPair* foldOrbitEnd(std::uint32_t codePoint);

}  // namespace neo::regex

#endif  // NEO_REGEX_UNICODE_H
