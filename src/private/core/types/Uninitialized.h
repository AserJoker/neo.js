// types/uninitialized.h - UninitializedType + UninitializedData
//
// uninitialized 是 TDZ（Temporal Dead Zone）哨兵类型：let/const 声明后、初始化前的
// 变量槽位持有此类型。Context 本身不消费它——由未来 VM 的 load 指令在读取变量时
// 检测并抛出 ReferenceError（"Cannot access before initialization"）。
//
// 所有行为方法返回 self（透传）：理论上 uninitialized 不会参与任何运算，
// 进入操作前应已在 VM load 阶段失败。透传实现仅为满足 IType 纯虚接口。
//
// 关联 ADR: 016（Type 体系修订）、030 阶段 5（define/store）

#ifndef NEO_CORE_PRIVATE_TYPES_UNINITIALIZED_H
#define NEO_CORE_PRIVATE_TYPES_UNINITIALIZED_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

class UninitializedType : public IType {
public:
    UninitializedType() : IType(TypeKind::Uninitialized) {}
    const char* name() const override { return "uninitialized"; }

    // 行为方法：全部透传 self（TDZ 哨兵不参与运算，进入前应已失败）
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

// 空负载 data（哨兵仅占类型标识）
class UninitializedData : public IData {
public:
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(UninitializedData));
        return new (p) UninitializedData();
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_UNINITIALIZED_H
