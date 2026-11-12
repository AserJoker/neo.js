// unicode/UnicodeClassification.h - Unicode 分类谓词
//
// 前端词法器需要的分类：
//   - ID_Start / ID_Continue（Unicode 属性，ECMAScript 标识符的基础）
//   - ECMAScript IdentifierStart/Part（ID_* 加上 $、_、ZWNJ、ZWJ）
//   - WhiteSpace / LineTerminator（ECMAScript 词法空白与行终止符）
//
// ID 表由 tools/gen_unicode_id_tables.py 从 Unicode 15.1.0
// DerivedCoreProperties.txt 生成，运行时不依赖 ICU。
//
// 关联 ADR: 052（前端源流）、053（Unicode 分类）

#ifndef NEO_UNICODE_UNICODE_CLASSIFICATION_H
#define NEO_UNICODE_UNICODE_CLASSIFICATION_H

#include <cstdint>

namespace neo::unicode {

// Unicode ID_Start / ID_Continue（不含 $、_；Unicode 15.1 的 ID_Continue
// 已包含 ZWNJ/ZWJ）。
bool isIdStart(uint32_t cp);
bool isIdContinue(uint32_t cp);

// ECMAScript IdentifierStart / IdentifierPart（ES2024 §12.7）：
//   IdentifierStart  = ID_Start | $ | _
//   IdentifierPart   = ID_Continue | $ | _ | ZWNJ | ZWJ
// （ZWNJ/ZWJ 在当前 Unicode 表中已属于 ID_Continue，显式判断用于语义自描述。）
bool isIdentifierStart(uint32_t cp);
bool isIdentifierContinue(uint32_t cp);

// ECMAScript WhiteSpace（ES2024 §12.2）：TAB/VT/FF/SP/NBSP/ZWNBSP + Zs。
bool isWhitespace(uint32_t cp);

// ECMAScript LineTerminator：LF / CR / U+2028 / U+2029。
bool isLineTerminator(uint32_t cp);

bool isWhitespaceOrLineTerminator(uint32_t cp);

}  // namespace neo::unicode

#endif  // NEO_UNICODE_UNICODE_CLASSIFICATION_H
