// types/array.h - ArrayType + ArrayData
//
// 数组（ADR-018 §6）：在对象模型基础上增加独立索引元素容器，命名属性/符号属性/
// 原型链复用 ObjectData（与 FunctionData 一致，ArrayData 继承 ObjectData）。
//
// 索引元素采用**混合存储**（稀疏语义）：
//   - 小数组（长度 ≤ kDenseLimit）用 std::vector<Value*>，nullptr 表示空洞；
//   - 一旦长度超过 kDenseLimit 或写入 index ≥ kDenseLimit，一次性迁移到
//     std::unordered_map<uint32_t, Value*>，此后恒为 map（不回切）。
//   空洞在 vector 中显式存在（nullptr），在 map 中天然缺席。
//
// length（ES2024 §10.4.2.4 ArraySetLength）：
//   - 读：元素容器长度（vector.size() 或 map 中最大 index + 1，由 length_ 维护）；
//   - 写：截断（丢弃 ≥ n 的元素）或扩展（vector 补 nullptr 空洞）；
//   - 描述符 writable=true / enumerable=false / configurable=false。
//
// 索引判定（ES2024 §7.1.19 CanonicalNumericIndexString 的数组子集）：
//   十进制数字串、无前导零（"0" 除外）、值 < 2^32-1 → 索引属性；否则走 properties。
//
// 关联 ADR: 015（data 持子 value）、018（对象模型 §6）、019（赋值语义 §2）

#ifndef NEO_CORE_PRIVATE_TYPES_ARRAY_H
#define NEO_CORE_PRIVATE_TYPES_ARRAY_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"  // ArrayData 继承 ObjectData
#include "infra/IAllocator.h"

#include <cstdint>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace neo::core {

class Value;

// 稠密/稀疏切换阈值：长度或写入索引达到此值即迁移到 map
inline constexpr uint32_t kArrayDenseLimit = 64;

// 数组最大长度（ES2024 §6.1.7.2：2^32 - 1）
inline constexpr uint32_t kArrayMaxLength = 0xFFFFFFFFu;

class ArrayType : public IType {
public:
    ArrayType() : IType(TypeKind::Array, true) {}
    const char* name() const override { return "array"; }

    // 索引型 exotic：索引元素 + length 自有属性（供对象模型枚举/自有属性判定复用）
    bool hasIndexedElements() const override { return true; }
    uint32_t indexedLength(Value* self) const override;
    Value* indexedElement(IContext* ctx, Value* self, uint32_t index) const override;
    bool indexedLengthWritable(Value* self) const override;

    // 索引与 length 的自有属性（逐元素描述符）。
    Value* getOwnProperty(IContext* ctx, Value* self, const PropertyKey& key, bool& exists,
                          Property& out) const override;
    Value* ownKeys(IContext* ctx, Value* self, std::vector<Value*>& out) const override;

    // 运算：ToPrimitive → join(",") → 按 JS 规则参与运算
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 数组不是函数也不是构造函数（Array 构造函数是 Function 类型）
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;

    // 属性操作：length 与索引属性本地处理，其余委托 ObjectType（命名/符号/原型链）
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

// 数组负载：继承 ObjectData 复用 properties/order/symbols/prototype/私有槽与 GC 遍历。
class ArrayData : public ObjectData {
public:
    // —— 索引元素：idx -> Property（与普通对象属性同构）——
    // 存在即自有属性；空洞 = 键缺席。正确性优先，本阶段不做稠密/稀疏优化。
    std::unordered_map<uint32_t, Property> elements;
    uint32_t length_ = 0;
    bool lengthWritable = true;

    uint32_t length() const { return length_; }

    // 读取索引元素的数据值；不存在或访问器返回 nullptr。
    Value* elementAt(uint32_t index) const;
    bool hasElement(uint32_t index) const;

    // 以默认数据属性写入索引（内部构造用），必要时扩展 length。
    void setElement(uint32_t index, Value* value);

    // 删除索引元素。返回删除前是否存在。
    bool removeElement(uint32_t index);

    // 设置 length：截断丢弃 ≥ n 的元素，扩展仅更新 length。
    void setLength(uint32_t n);

    // GC 遍历：先报告继承的对象字段（prototype/properties/symbols），再报告索引元素。
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，元素容器随之共享。
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

// 数组 ToString（ES2024 §7.1.14：join(",")，null/undefined 元素按空串）。
std::string arrayToString(const ArrayData* d);

// 解析数组索引键（十进制数字串、无前导零、< 2^32-1）。成功返回 true 并写入 out。
bool parseArrayIndex(const std::string& key, uint32_t& out);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_ARRAY_H
