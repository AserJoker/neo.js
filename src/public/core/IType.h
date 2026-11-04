// IType.h - Value 的 meta（IType）抽象接口（ADR-016 修订）
//
// IType 封装 Value 的多态行为，无状态，每种类型一个全局单例。Value 持 IType* 共享，
// 无须引用计数，GC 不跟踪 IType（生命周期与进程一致）。
// TypeKind 枚举用于快速判同类型、分派调试、序列化。
//
// 本阶段（M2 阶段 3）只实现 kind + name，行为方法（add/call/get_property/...）
// 签名带 Context*，待 Context（阶段 4）引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）、006（异常即值）

#ifndef NEO_CORE_ITYPE_H
#define NEO_CORE_ITYPE_H

#include <cstddef>
#include <cstdint>

namespace neo::core {

// 前向声明
class IContext;
class Value;

// 类型枚举：快速判同类型、分派调试、序列化
enum class TypeKind : uint8_t {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Exception,      // 可捕获异常（ADR-017 §3）
    Fatal,          // 不可捕获致命错误（ADR-017 §4）
    Uninitialized,  // TDZ 哨兵（let/const 初始化前，VM load 时消费）
    Function,       // 函数对象（ADR-021，本阶段仅 native）
    // 后续阶段：Object/Array/Symbol/BigInt/Interrupt/...
};

// Value 的 meta：抽象接口，每种 JS 类型一个实现类（单例）。
// 无状态——所有行为纯函数（按 ctx + 操作数计算），本阶段仅 kind + name。
class IType {
    TypeKind kind_;

public:
    explicit IType(TypeKind k) : kind_(k) {}
    virtual ~IType() = default;

    TypeKind kind() const { return kind_; }
    virtual const char* name() const = 0;

    // —— 行为接口（ADR-016 修订 §3）——
    // 统一签名 Value* op(IContext* ctx, Value* a, Value* b) / Value* op(IContext* ctx, Value* self)。
    // 分派规则：永远左操作数的 type 执行（ctx->add(a,b) → a->type()->add(ctx,a,b)）。
    // 跨类型转换：左 type 按所实现操作的 JS 规则主动调换左右（ctx->add(b,a)）。
    // 兜底：返回 NaN 或 Exception（IType 自负责操作有效性）。
    // 全部返回 Value*（正常/Exception/Fatal 同一通道，调用点 Fatal 检查，ADR-006 + ADR-017 §4）。
    // Exception/Fatal 不参与计算——行为方法入口拦截透传（ADR-006 异常即值 + ADR-017 §4）。

    // 二元运算（左操作数 type 执行）
    virtual Value* add(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* sub(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* mul(IContext* ctx, Value* a, Value* b) = 0;
    virtual Value* div(IContext* ctx, Value* a, Value* b) = 0;

    // 单目
    virtual Value* toString(IContext* ctx, Value* self) = 0;

    // 调用（ADR-005 native 等价 + ADR-022 统一入口）：
    // ctx->call(fn, self, argc, argv) → fn->type()->call(ctx, fn, self, argc, argv)。
    // 非函数类型的 call 返回 Exception("xxx is not a function")。
    // FunctionType 校验 minArgc 后调 native 指针。
    // Exception/Fatal 透传 self（不参与调用）。
    virtual Value* call(IContext* ctx, Value* self, Value* thisArg,
                        size_t argc, Value* argv[]) = 0;
};

// —— IType 单例获取（具体实现在 private/core/Types.cpp）——
// 每种类型一个全局单例，Value 持 IType* 共享。返回的指针生命周期与进程一致。
IType* getUndefinedType();
IType* getNullType();
IType* getBooleanType();
IType* getNumberType();
IType* getStringType();
IType* getExceptionType();
IType* getFatalType();
IType* getUninitializedType();
IType* getFunctionType();

}  // namespace neo::core

#endif  // NEO_CORE_ITYPE_H
