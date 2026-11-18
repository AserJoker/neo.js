// StringBuiltins.h - String.prototype 常用方法（ES2024 §22.1.3）
//
// 所有方法按 UTF-16 码元语义实现：输入 UTF-8 先转码元序列，操作后再转回。
// 大小写映射使用 regex 模块的简单折叠表（无 full/special casing）。

#ifndef NEO_CORE_PRIVATE_STRING_BUILTINS_H
#define NEO_CORE_PRIVATE_STRING_BUILTINS_H

namespace neo::core {

class IContext;
class Value;

// 注册 String.prototype 方法（toString/valueOf 由包装类注册）。
bool registerStringBuiltins(IContext* ctx, Value* proto);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_STRING_BUILTINS_H
