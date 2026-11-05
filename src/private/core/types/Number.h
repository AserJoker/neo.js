// types/number.h - NumberType + NumberData
//
// number 类型：持 double 负载。行为方法（add/sub/mul/div/to_string/...）待 Context
// 引入后补充——预计本文件会随 NumberType 行为实现大量膨胀。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_NUMBER_H
#define NEO_CORE_PRIVATE_TYPES_NUMBER_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

class NumberType : public IType {
public:
    NumberType() : IType(TypeKind::Number) {}
    const char* name() const override { return "number"; }

    // 行为方法（ADR-016 §3，实现见 Number.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* setProperty(IContext* ctx, Value* self, std::string_view key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* deleteProperty(IContext* ctx, Value* self, std::string_view key) override;
    Value* defineProperty(IContext* ctx, Value* self, std::string_view key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 持 double 的扁平 data
class NumberData : public IData {
public:
    double value;
    explicit NumberData(double v) : value(v) {}
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(NumberData));
        return new (p) NumberData(value);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_NUMBER_H
