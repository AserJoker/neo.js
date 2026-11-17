// RegExpBuiltins.h - RegExp 类型/构造器/prototype 与 String 方法接入（ADR-062 R4）
//
// 关联 ADR: 020（标准库组织）、062（正则引擎）

#ifndef NEO_CORE_PRIVATE_REGEXP_BUILTINS_H
#define NEO_CORE_PRIVATE_REGEXP_BUILTINS_H

#include "regex/RegexFlags.h"
#include "regex/RegexProgram.h"

#include <memory>
#include <string>
#include <string_view>

namespace neo::core {

class IContext;

// 编译 pattern + flags。成功返回 true 并写入 program/outFlags；
// 失败返回 false 并写入 error（供 JS 层映射为 SyntaxError）。
bool compileRegExp(std::string_view pattern, std::string_view flags,
                   std::shared_ptr<const neo::regex::Program>& program,
                   neo::regex::Flags& outFlags, std::string& error);

// 注册 RegExp 构造函数/prototype/Symbol 方法，并给 String.prototype
// 补 match/matchAll/replace/replaceAll/search/split。
// 依赖 Object/Function/String 内建已就绪。
bool registerRegExpBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_REGEXP_BUILTINS_H
