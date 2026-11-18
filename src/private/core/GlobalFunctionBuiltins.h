// GlobalFunctionBuiltins.h - 全局函数（ES2024 §19.2）
//
// parseInt / parseFloat / isNaN / isFinite / encodeURI* / decodeURI*。
// 这些函数挂在 global object 上（全局环境记录的对象记录）。

#ifndef NEO_CORE_PRIVATE_GLOBAL_FUNCTION_BUILTINS_H
#define NEO_CORE_PRIVATE_GLOBAL_FUNCTION_BUILTINS_H

namespace neo::core {

class IContext;

// 注册全局函数。依赖 Object.prototype / Function.prototype 已就绪。
bool registerGlobalFunctionBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_GLOBAL_FUNCTION_BUILTINS_H
