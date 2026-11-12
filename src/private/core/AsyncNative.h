// AsyncNative.h - native async 函数的协程驱动与业务临时变量槽
//
// native async 函数以阶段机（switch(resumePoint)）编写：
//   - C++ 局部变量只在当前 stage 内有效，返回 Interrupt 后栈帧销毁；
//   - 业务临时变量必须经 storeNativeLocal 写入函数 execScope
//     （ctx->currentFunctionEnv()），跨 Await stage 持久。
//
// 错误通道：native 直接返回 Exception；await 拒绝时 driver 以
// createException(reason) 作为 sentValue 恢复协程。Interrupt(Throw) 仅 VM 专用。
//
// 关联 ADR: 045（协程抽象）、046（Interrupt 控制信号）

#ifndef NEO_CORE_PRIVATE_ASYNC_NATIVE_H
#define NEO_CORE_PRIVATE_ASYNC_NATIVE_H

#include <cstddef>
#include <string_view>

namespace neo::core {

class IContext;
class Value;

// 读取业务临时变量；不存在返回 nullptr。
Value* loadNativeLocal(IContext* ctx, std::string_view name);

// 写入业务临时变量：绑定到当前函数 execScope 并登记 GC 跟踪。
// 同名旧值从跟踪中移除（只解除本作用域登记，不释放值）。
void storeNativeLocal(IContext* ctx, std::string_view name, Value* value);

// 以 native 函数为函数体运行 async 语义，返回 Promise。
// 函数体通过返回 Interrupt(Await, nextStage, awaitable) 挂起；恢复时
// sentValue 为正常值或 Exception（拒绝）。完成时正常值 resolve、
// Exception/Fatal reject。调用方负责 minArgc 等前置校验。
Value* runAsyncNative(IContext* ctx, Value* fn, Value* thisArg, size_t argc,
                      Value* argv[]);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_ASYNC_NATIVE_H
