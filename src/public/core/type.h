// type.h - Value 的 meta（Type）抽象接口（ADR-016 修订）
//
// Type 封装 Value 的多态行为，无状态，每种类型一个全局单例。Value 持 Type* 共享，
// 无须引用计数，GC 不跟踪 Type（生命周期与进程一致）。
// TypeKind 枚举用于快速判同类型、分派调试、序列化。
//
// 本阶段（M2 阶段 3）只实现 kind + name，行为方法（add/call/get_property/...）
// 签名带 Context*，待 Context（阶段 4）引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）、006（异常即值）

#ifndef NEO_CORE_TYPE_H
#define NEO_CORE_TYPE_H

#include <cstdint>

namespace neo::core {

// 类型枚举：快速判同类型、分派调试、序列化
enum class TypeKind : uint8_t {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Exception,  // 可捕获异常（ADR-017 §3）
    Fatal,      // 不可捕获致命错误（ADR-017 §4）
    // 后续阶段：Object/Function/Array/Symbol/BigInt/Interrupt/...
};

// Value 的 meta：抽象接口，每种 JS 类型一个实现类（单例）。
// 无状态——所有行为纯函数（按 ctx + 操作数计算），本阶段仅 kind + name。
class Type {
    TypeKind kind_;

public:
    explicit Type(TypeKind k) : kind_(k) {}
    virtual ~Type() = default;

    TypeKind kind() const { return kind_; }
    virtual const char* name() const = 0;

    // —— 行为接口（ADR-016 修订 §3）——
    // 统一签名 Value* op(Context* ctx, Value* a, Value* b) / Value* op(Context* ctx, Value* self, ...)。
    // 分派规则：永远左操作数的 type 执行（ctx->add(a,b) → a->type()->add(ctx,a,b)）。
    // 跨类型转换：左 type 按所实现操作的 JS 规则主动调换左右（ctx->add(b,a)）。
    // 兜底：返回 NaN 或 Exception（Type 自负责操作有效性）。
    // 全部返回 Value*（正常/Exception/Fatal 同一通道，调用点 Fatal 检查，ADR-006 + ADR-017 §4）。
    // 本阶段不声明行为方法（待 Context 引入后补充）。
};

// —— Type 单例获取（具体实现在 private/core/types.cpp）——
// 每种类型一个全局单例，Value 持 Type* 共享。返回的指针生命周期与进程一致。
Type* get_undefined_type();
Type* get_null_type();
Type* get_boolean_type();
Type* get_number_type();
Type* get_string_type();
Type* get_exception_type();
Type* get_fatal_type();

}  // namespace neo::core

#endif  // NEO_CORE_TYPE_H
