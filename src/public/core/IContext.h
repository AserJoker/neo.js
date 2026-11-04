// IContext.h - Context 门面（引擎入口，整合 GC/IAllocator/Console + Scope 树）
//
// IContext 是引擎对外门面（ADR-011 §2/§3）：持有 GC 所有权（内部创建并销毁），
// 注入 IAllocator 和 Console（无所有权，由外部销毁），持有 global/root/current
// 三层 scope（ADR-012 §1）。
//
// GC 由 Context 持有所有权：Context 用注入的 IAllocator 创建 GC（createGcEngine(alloc)），
// 析构时由 Context 销毁 GC。IAllocator 和 Console 是注入资源，Context 不持有所有权。
//
// 三层 scope（ADR-012 + ADR-030 阶段 4）：
//   global —— 根作用域（parent=nullptr，永久，Context 存活期间一直在）
//   root   —— 模块根作用域（parent=global，Context 存活期间一直在）
//   current —— 当前作用域（Scope 的 parent 链即调用栈，popScope 回到 parent），初始=root
//
// 析构顺序：先销毁 global_scope（~Scope 递归销毁整棵 scope 树：current 栈残留 → root → global），
// 再销毁 GC（GC 析构时 collect 回收残留 Value，若有泄漏则 panic），
// IAllocator/Console 由外部销毁（Context 不持有所有权）。
//
// 接口-实现-工厂（ADR-010）：IContext 抽象接口在 public/core/，Context 在 private/core/，
// 工厂 createContext(alloc, console) 返回接口指针。
//
// 关联 ADR: 010（接口-实现-工厂）、011（Context 门面 + 内存归属）、012（Scope 树）、030 阶段 4

#ifndef NEO_CORE_ICONTEXT_H
#define NEO_CORE_ICONTEXT_H

#include "infra/IAllocator.h"
#include "infra/Console.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace neo::core {

// 前向声明
class IGCEngine;
class Scope;
class Value;

// native 函数指针签名（ADR-005 §3）：ctx=语言上下文，self=this 绑定，
// argc/argv=参数，返回 Value*（正常/异常同一通道）。
class IContext;
using NativeFn = Value* (*)(IContext* ctx, Value* self, size_t argc, Value* argv[]);

// 引擎门面：持有 GC 所有权 + 注入 IAllocator/Console + Scope 树
class IContext {
public:
    virtual ~IContext() = default;

    // —— 资源访问 ——
    // allocator/console: 注入资源（无所有权，由外部销毁）
    // gc: Context 持有所有权（内部创建并销毁）
    virtual infra::IAllocator* allocator() const = 0;
    virtual IGCEngine* gc() const = 0;
    virtual infra::Console* console() const = 0;

    // —— 三层 scope 访问 ——
    virtual Scope* global() const = 0;
    virtual Scope* root() const = 0;
    virtual Scope* current() const = 0;

    // —— scope 切换（ADR-022 函数调用）——
    // Scope 的 parent 链本身就是调用栈，Context 只需保存 root/current 两个指针，无须额外栈。
    // setRootScope: 切换当前模块根作用域（函数调用时切到被调函数的 rootScope）。
    // setCurrentScope: 切换当前作用域（函数调用时切到被调函数的临时执行作用域）。
    // 调用方负责保存/恢复。
    virtual void setRootScope(Scope* s) = 0;
    virtual void setCurrentScope(Scope* s) = 0;

    // —— current scope 管理 ——
    // pushNewScope: 在 allocator 上新建 parent 的子 scope 并设为 current。
    // 用于函数调用的临时执行作用域（ADR-022）。返回新建的 scope，OOM 返回 nullptr。
    virtual Scope* pushNewScope(Scope* parent) = 0;

    // popScope: 销毁 current scope 并回到其 parent（~Scope 递归释放子树 + allocator->free）。
    // global/root 不可 pop（parent 为 global 或为 nullptr 时无操作）。
    virtual void popScope() = 0;

    // —— Value 创建便捷接口（ADR-030 阶段 4）——
    // 内部封装 gc->allocData<T> + gc->createValue(type, data)，把 Value 加入 current scope
    // 跟踪（scopeRef++）。可选 name：指定则同时 bind 到 current scope（命名绑定）。
    // 返回 Value*：正常结果，或内存耗尽时返回 OOM 哨兵（ADR-031，非 nullptr）。
    virtual Value* createUndefined(std::string_view name = {}) = 0;
    virtual Value* createNull(std::string_view name = {}) = 0;
    virtual Value* createBoolean(bool b, std::string_view name = {}) = 0;
    virtual Value* createNumber(double n, std::string_view name = {}) = 0;
    virtual Value* createString(std::string_view s, std::string_view name = {}) = 0;

    // —— 运算代理（ADR-016 §4/§7）——
    // 纯代理：ctx->add(a,b) → a->type()->add(this, a, b)。
    // 外部代码不直接调 type->op()，一律走 Context。
    virtual Value* add(Value* a, Value* b) = 0;
    virtual Value* sub(Value* a, Value* b) = 0;
    virtual Value* mul(Value* a, Value* b) = 0;
    virtual Value* div(Value* a, Value* b) = 0;
    virtual Value* toString(Value* self) = 0;

    // —— 函数调用（ADR-022）——
    // call: 纯代理 ctx->call(fn, thisArg, argc, argv) → fn->type()->call(this, fn, thisArg, argc, argv)。
    //       非函数类型返回 Exception("xxx is not a function")。
    virtual Value* call(Value* fn, Value* thisArg, size_t argc, Value* argv[]) = 0;

    // createNativeFunction: 创建 native 函数 Value。
    //   closureScope = 当前 root scope 的子作用域（新建，Sema 收集的穿透闭包变量绑定于此）；
    //   rootScope = 当前 root scope（借用）；
    //   minArgc = 最少参数数量（调用时校验）；
    //   bindName = 变量绑定名（非空则 bind 到 current scope）。
    // 函数名不在此设置（缺省 "<anonymous>"），由 setFunctionName 单独设置——
    // 对应 VM 字节码序列 define → store → setname。
    // 返回 Value*：正常函数值，或 OOM 哨兵（ADR-031）。
    virtual Value* createNativeFunction(NativeFn fn, size_t minArgc,
                                        std::string_view bindName = {}) = 0;

    // setFunctionName: 设置函数名（FunctionData.name）。仅对 Function 类型有效，
    // 其他类型忽略。对应 VM 的 setname 字节码。
    virtual void setFunctionName(Value* fn, std::string_view name) = 0;

    // setClosure: 把当前作用域链上名为 name 的变量加入函数的闭包作用域（ADR-013 §2）。
    // 绑定 = 共享同一 Value（不复制），closureScope 加入时 scopeRef++。
    // 查找沿 current 的 parent 链（lookupChain）。
    // 返回 fn（成功），或 Exception（找不到变量 / fn 非函数）。
    virtual Value* setClosure(Value* fn, std::string_view name) = 0;

    // —— 错误值创建（ADR-006 异常即值 + ADR-017 §4 Fatal）——
    // createException: 创建可捕获异常 Value（child 永远不为 nullptr，无错误对象时传 undefined Value）。
    // createFatal: 创建不可捕获致命错误 Value（字面量消息，静态存储期）。
    virtual Value* createException(Value* child) = 0;
    virtual Value* createFatal(const char* msg) = 0;

    // —— OOM 哨兵（ADR-031）——
    // Context 构造时预分配一个 Fatal 类型 Value，挂载到 global（存活于 Context 生命周期）。
    // create* 内存耗尽时返回此单例，调用方用指针比较判断是否 OOM（v == ctx->oomValue()）。
    // OOM 属 Fatal（不可捕获），调用点检查到 Fatal 立即透传（ADR-017 §4）。
    virtual Value* oomValue() const = 0;

    // —— 变量定义与赋值（ADR-030 阶段 5）——
    // define: 在 current scope 定义变量名。clone source 的 data 进新槽位。
    //         同 scope 重复定义无条件返回 Exception（var/let/const 差异已在 Sema 抹平）。
    //         source 若是 Exception/Fatal 直接透传。
    // store: 从 current scope 开始向上查找 name，找到则 clone source 的 data 替换其 type+data。
    //        找不到时：按 "strict" 标注分派——严格返回 Exception，非严格在 global 定义。
    //        source 若是 Exception/Fatal 直接透传。
    virtual Value* define(std::string_view name, Value* source) = 0;
    virtual Value* store(std::string_view name, Value* source) = 0;

    // —— 可赋值性（ADR-030 阶段 5）——
    // setMutable: 设置 Value 是否可赋值（const 声明由 Sema 置 false）。
    // isMutable: 查询 Value 是否可赋值。
    // store 对 mutable=false 的 target 返回 Exception（禁止赋值）。
    virtual void setMutable(Value* v, bool m) = 0;
    virtual bool isMutable(Value* v) const = 0;

    // —— 作用域标注（ADR-030 阶段 5）——
    // setFlag: 在当前 scope 标注 name（如 "strict"）。语法上仅模块作用域/函数体可标注。
    // hasFlag: 从当前 scope 沿 parent 链向上查找标注（内层继承外层）。
    // 当前 global scope 默认标注 "strict"（保持默认严格模式）。
    virtual void setFlag(std::string_view name) = 0;
    virtual bool hasFlag(std::string_view name) const = 0;
};

// 工厂：创建 Context，注入 IAllocator/Console（无所有权，外部销毁）。
// Context 内部用 IAllocator 创建 GC（持有所有权）+ global/root scope。
std::unique_ptr<IContext> createContext(infra::IAllocator* alloc,
                                         infra::Console* console);

}  // namespace neo::core

#endif  // NEO_CORE_ICONTEXT_H
