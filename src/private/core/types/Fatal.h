// types/fatal.h - FatalType + FatalData
//
// fatal 类型（ADR-017 §4）：不可捕获致命错误，持 const char* 字面量字符串（静态存储期，
// 构造时确定，硬编码如 "OOM"）。不经 IAllocator 分配，GC 无须跟踪。
// FatalData 对象本身仍经 allocData 分配（Data 基类部分），但字符串负载是字面量。
// 行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（Fatal 类型）

#ifndef NEO_CORE_PRIVATE_TYPES_FATAL_H
#define NEO_CORE_PRIVATE_TYPES_FATAL_H

#include "core/IData.h"
#include "core/IType.h"
#include "infra/IAllocator.h"

#include <new>
#include <string_view>

namespace neo::core {

class FatalType : public IType {
public:
    FatalType() : IType(TypeKind::Fatal) {}
    const char* name() const override { return "fatal"; }

    // 行为方法：全部透传 self（ADR-017 §4 Fatal 透传，实现见 Fatal.cpp）
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
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

// 持 const char*（字面量字符串，静态存储期，无须释放）
class FatalData : public IData {
public:
    const char* message;  // 字面量字符串，静态存储期，无须释放
    explicit FatalData(const char* msg) : message(msg) {}
    IData* clone(neo::infra::IAllocator* alloc) const override {
        void* p = alloc->alloc(sizeof(FatalData));
        return new (p) FatalData(message);  // 复制指针，字面量静态存储期
    }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_FATAL_H
