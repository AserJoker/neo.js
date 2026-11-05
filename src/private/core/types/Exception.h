// types/exception.h - ExceptionType + ExceptionData
//
// exception 类型（ADR-017 §3）：可捕获异常，持 Value* child（被抛出的错误对象）。
// 本阶段唯一复杂类型：data 持 GC 管的子 value，须覆写 enumerateChildren 让 GC 沿
// 引用链标记被引用的错误对象（ADR-015 data 层持子 value 而非子 data，无环）。
//
// **child 永远不为 nullptr**：不携带错误对象时用 undefined Value 表示，而非空指针。
// 行为方法待 Context 引入后补充。
//
// 关联 ADR: 015（data 引用计数分层）、016（Type 体系修订）、017（Exception 类型）

#ifndef NEO_CORE_PRIVATE_TYPES_EXCEPTION_H
#define NEO_CORE_PRIVATE_TYPES_EXCEPTION_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

// 前向声明
class Value;

class ExceptionType : public IType {
public:
    ExceptionType() : IType(TypeKind::Exception) {}
    const char* name() const override { return "exception"; }

    // 行为方法：全部透传 self（ADR-006 异常即值，实现见 Exception.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 持 Value*（被抛出的错误对象），本阶段唯一复杂类型
class ExceptionData : public IData {
public:
    Value* child;  // 被抛出的错误对象（GC 管的 Value*，永远不为 nullptr）
    explicit ExceptionData(Value* c) : child(c) {}

    // 覆写：向 visitor 报告持有的子 value，让 GC 沿引用链标记
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：共享 child 指针（Value* 由 GC 管理，引用计数保证安全）
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(ExceptionData));
        return new (p) ExceptionData(child);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_EXCEPTION_H
