// types/Arguments.h - arguments exotic 对象（ES2024 §10.4.4）
//
// arguments 是普通对象 + 索引元素 + length + callee，并可能带「参数映射」：
// 非严格模式且形参列表为简单标识符时，索引 i 与第 i 个形参绑定保持活别名
// （mapped arguments）。映射直接读写函数环境作用域的绑定槽位，不复制值。
//
// 作用域生命周期：arguments 可能逃逸出函数（return arguments），此时函数
// 环境作用域已 pop。ArgumentsData 在创建时对作用域 incCaptureRef()，popScope
// 会把它转为 autoDispose 孤儿；对象析构时释放 pin，最后一个引用者回收作用域。
// GC 标记经 enumerateChildren 主动 visitScope，保证逃逸对象仍能保活环境。
//
// 索引元素与 length 走 IType 的 indexed 能力（与 Array/String 包装一致）；
// 映射索引的读取由 indexedElement 实时查绑定，写入由 defineProperty 拦截后
// 写回绑定（Context::storeBinding）。删除或改为访问器/收紧特性时解除映射，
// 回落为普通自有属性。
//
// 关联 ADR: 018（对象模型）、033（eval 与隔离函数）、036（闭包作用域孤立化）、
//          044（GC 可达性）、047（函数环境记录）

#ifndef NEO_CORE_PRIVATE_TYPES_ARGUMENTS_H
#define NEO_CORE_PRIVATE_TYPES_ARGUMENTS_H

#include "core/IContext.h"
#include "core/IType.h"
#include "core/types/Object.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neo::core {

class Scope;

class ArgumentsType : public ObjectType {
public:
    ArgumentsType() : ObjectType(TypeKind::Arguments) {}
    const char* name() const override { return "arguments"; }

    bool hasIndexedElements() const override { return true; }
    uint32_t indexedLength(Value* self) const override;
    Value* indexedElement(IContext* ctx, Value* self, uint32_t index) const override;
    bool indexedWritable(Value* self) const override;
    bool indexedConfigurable(Value* self) const override;
    bool indexedLengthWritable(Value* self) const override;

    Value* defineProperty(IContext* ctx, Value* self, Value* key, Value* value,
                          bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable,
                          bool configurable) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
};

class ArgumentsData : public ObjectData {
public:
    Scope* scope = nullptr;               // 函数环境（pin 住，逃逸时保活）
    Value* callee = nullptr;              // mapped callee（GC 遍历）
    std::vector<std::string> mappedNames; // index → 形参名；空 = unmapped
    uint32_t length = 0;                  // 实际实参个数
    bool lengthWritable = true;           // length 属性的 writable

    ~ArgumentsData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
};

IType* getArgumentsType();

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ARGUMENTS_H
