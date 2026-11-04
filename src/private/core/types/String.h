// types/string.h - StringType + StringData
//
// string 类型：持 std::string 负载，不可变（构造后不再修改，"修改"操作返回新 StringData）。
// 行为方法（concat/slice/indexOf/to_string/...）待 Context 引入后补充——预计本文件
// 会随 StringType 行为实现大量膨胀。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_STRING_H
#define NEO_CORE_PRIVATE_TYPES_STRING_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string>
#include <string_view>

namespace neo::core {

class StringType : public IType {
public:
    StringType() : IType(TypeKind::String) {}
    const char* name() const override { return "string"; }

    // 行为方法（ADR-016 §3，实现见 String.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
};

// 持 std::string 的 data，不可变（构造后不再修改）
// 任何"修改"操作（concat/replace/...）返回新 StringData。
class StringData : public IData {
public:
    std::string value;
    explicit StringData(std::string v) : value(std::move(v)) {}
    explicit StringData(std::string_view v) : value(v) {}
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(StringData));
        return new (p) StringData(value);  // 拷贝 std::string
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_STRING_H
