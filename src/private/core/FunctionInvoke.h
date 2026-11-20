// FunctionInvoke.h - native 函数调用核心（供 FunctionType 与协程 runner 复用）
//
// invokeNativeFunction 承载 ADR-022 §3 的完整调用流程（作用域切换/恢复 +
// 返回值归属 + 栈平衡），并含 ADR-045 §5 的挂起分支：
//   - 当前处于协程恢复中（ctx->getCurrentCoroutine() 非空）且函数体返回
//     Interrupt(Yield) 时，不销毁 execScope，而是交给协程对象并孤立化；
//   - 恢复时由 runner 传入 resumeScope（协程保存的孤立子树）重新挂上。
//
// 关联 ADR: 022（函数调用）、036（闭包作用域孤立化）、045（协程抽象）

#ifndef NEO_CORE_PRIVATE_FUNCTION_INVOKE_H
#define NEO_CORE_PRIVATE_FUNCTION_INVOKE_H

#include "core/IContext.h"  // NativeFn

#include <cstddef>

namespace neo::vm {
struct VmState;
}  // namespace neo::vm

namespace neo::core {

class Scope;
class Value;

// 以 thisArg/argc/argv 调用 fn 的 native 函数体。
//   overrideFn：构造钩子等替代函数体（nullptr = 用 d->native）。
//   resumeScope：协程恢复时重挂的保存作用域（nullptr = 新建临时执行作用域）。
//   newTarget：本次调用的 new.target（nullptr = undefined；构造调用由调用方传入）。
//   outThis：非空时写出函数环境的最终 this（派生类构造器可能由 super() 初始化）。
// fn 必须是 Function 类型且非绑定函数；返回正常值/Exception/Fatal/Interrupt。
Value* invokeNativeFunction(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                            Value* argv[], NativeFn overrideFn = nullptr,
                            Scope* resumeScope = nullptr, Value* newTarget = nullptr,
                            Value** outThis = nullptr);

// 以 thisArg/argc/argv 调用 fn 的 script 函数体（ADR-060 §9）：
// 切换作用域、创建新 VM、按序压入形参与 self，从 entryPc 执行。
// fn 必须是 isScript 的 Function；返回正常值/Exception/Fatal/Interrupt。
// newTarget：构造调用传入 new.target（nullptr = 普通调用，new.target 为 undefined）。
// outThis：非空时写出函数环境的最终 this（派生类构造器可能由 super() 初始化）。
Value* invokeScriptFunction(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                            Value* argv[], Value* newTarget = nullptr,
                            Value** outThis = nullptr);

// 生成器调用前置：同步执行参数绑定到 BodyStart，成功时返回已初始化的
// execScope 与挂起快照（由调用方交给协程对象持有）；失败时 error 非空。
struct GeneratorPrelude {
    Value* error = nullptr;
    Scope* execScope = nullptr;
    vm::VmState* vmState = nullptr;
};

GeneratorPrelude prepareGeneratorCoroutine(IContext* ctx, Value* fn,
                                           Value* thisArg, size_t argc,
                                           Value* argv[]);

// script 生成器的恢复驱动（CoroutineRunner）：首次恢复创建执行作用域并运行
// 到首个 yield；后续恢复重建 VM 并从 VmState 继续。
Value* resumeScriptCoroutine(IContext* ctx, Value* coroutine, Value* sentValue);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_FUNCTION_INVOKE_H
