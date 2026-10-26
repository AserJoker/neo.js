/*
 * 内部 C++ 实现：VM（字节码解释器核心）。
 *
 * 设计（架构 §5.4 执行模型 + §6 内存管理）：
 *   - 栈式 VM：操作数栈 + 调用栈帧（Frame）。
 *   - 主循环：`for (;;) { Opcode op = *pc++; switch (op) { ... } }`。
 *   - 调用栈帧（Frame）作为 GC 根集：操作数栈槽 + 局部变量槽 + 函数/
 *     闭包值都进 markRoots（用户已确认“栈帧入 GC 根集”）。
 *   - 异常：经 ConversionResult（value=nullptr + error）传递（#3/#4），
 *     不跨 C ABI 抛 C++ 异常。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_RUNTIME_VM_H
#define NEOJS_RUNTIME_VM_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "neojs/compiler/bytecode.h"
#include "neojs/runtime/conversion.h"

namespace neojs {

class JSContext;
class JSValue;
class JSEnvironment;
class JSFunction;

/* 执行字节码（顶层入口，架构 §5.4）。
 *   fn    函数字节码（顶层程序也是一个函数）。
 *   thisVal this 绑定（可空）。
 *   args  入口参数（内部值指针数组；可空当 argc==0）。
 *   env   当前函数的环境（闭包词法环境，M2.5；顶层程序为 null）。
 *   self  当前函数对象（M5.1d-b super 用：SuperLoadProp/Elem 从
 *         self->homeObject() 的 [[Prototype]] 读取；无 super 上下文为 null）。
 *   start_pc   起始指令位置（默认 0；M5.4 async 恢复用：从挂起点继续）。
 *   restore_stack 恢复用操作数栈（默认空；M5.4 async 恢复时传入挂起帧的
 *               栈副本，替代局部槽初始化）。
 *   initial_thrown 启动即注入的 JS 异常值（默认 nullptr；M5.4 await reject
 *               续延用：rejection 值作为异常从挂起点后第一条指令进入，经
 *               tryDispatch 派发到 try/catch 或向上传播）。
 * 返回 ConversionResult：成功 value 为结果值，失败 value=nullptr + error；
 * 挂起（M5.4 await）时 suspended=true（value/error 无效）。 */
ConversionResult runBytecode(JSContext *ctx, const FunctionBytecode &fn,
                             JSValue *thisVal, JSValue *const *args,
                             size_t argc, JSEnvironment *env = nullptr,
                             JSFunction *self = nullptr,
                             size_t start_pc = 0,
                             std::vector<JSValue *> *restore_stack = nullptr,
                             JSValue *initial_thrown = nullptr);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_VM_H
