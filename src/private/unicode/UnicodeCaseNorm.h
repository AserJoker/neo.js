// unicode/UnicodeCaseNorm.h - 全量大小写映射与 Unicode 归一化
//
// 大小写：UnicodeData.txt 的简单映射 + SpecialCasing.txt 的无条件多码点映射，
// 并实现 Final_Sigma 条件映射（依赖 Cased / Case_Ignorable）。
// 归一化：NFC / NFD / NFKC / NFKD（UAX #15），含 Hangul 算法分解/组合。
//
// 数据表由 tools/gen_unicode_case_norm_tables.py 从 Unicode 15.1.0 UCD 生成，
// 运行时不依赖 ICU。
//
// 关联 ADR: 053（Unicode 分类）

#ifndef NEO_UNICODE_UNICODE_CASE_NORM_H
#define NEO_UNICODE_UNICODE_CASE_NORM_H

#include <string>
#include <string_view>

namespace neo::unicode {

// 全量大小写映射（输入/输出均为 UTF-8/WTF-8）。
std::string toLowerFull(std::string_view utf8);
std::string toUpperFull(std::string_view utf8);

enum class NormalizationForm { NFC, NFD, NFKC, NFKD };

// 解析 "NFC"/"NFD"/"NFKC"/"NFKD"；不匹配返回 false。
bool parseNormalizationForm(std::string_view form, NormalizationForm& out);

// 按指定形式归一化（输入/输出均为 UTF-8/WTF-8）。
std::string normalize(std::string_view utf8, NormalizationForm form);

}  // namespace neo::unicode

#endif  // NEO_UNICODE_UNICODE_CASE_NORM_H
