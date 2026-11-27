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
#include <unordered_map>

namespace neo::core {

class StringData;

// 字符串池：内容 -> StringData*。键是 StringData::value 的 string_view（不可变、
// 堆稳定），池本身不持有引用；StringData 在构造时注册、析构（ref 归 0）时移除。
// 因此池中只含存活 data，最后一个引用者消失即自动消失。
using StringPool = std::unordered_map<std::string_view, StringData*>;

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
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    Value* hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 持 std::string 的 data，不可变（构造后不再修改）
// 任何"修改"操作（concat/replace/...）返回新 StringData。
class StringData : public IData {
public:
    std::string value;
    // 所属字符串池（可空：测试/独立构造不注册）。构造时注册，析构时移除。
    StringPool* pool_ = nullptr;

    explicit StringData(std::string v, StringPool* pool = nullptr)
        : value(std::move(v)), pool_(pool) {
        registerSelf();
    }
    explicit StringData(std::string_view v, StringPool* pool = nullptr)
        : value(v), pool_(pool) {
        registerSelf();
    }
    // 最后一个 Value 释放后由 GC sweep 析构：先移除池项，再销毁 value。
    ~StringData() override {
        if (pool_ != nullptr) pool_->erase(std::string_view(value));
    }

    // clone：不可变类型共享 data（ADR-019 §4）——ref++ 后返回 this，不复制字符串。
    IData* clone(neo::infra::IAllocator* /*alloc*/) const override {
        auto* self = const_cast<StringData*>(this);
        self->refInc();
        return self;
    }

private:
    void registerSelf() {
        if (pool_ != nullptr) pool_->emplace(std::string_view(value), this);
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_STRING_H
