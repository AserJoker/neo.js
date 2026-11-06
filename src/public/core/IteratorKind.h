// IteratorKind.h - 数组迭代器种类（ES2024 §23.1.5.1）
//
// Array.prototype.keys / values / entries 返回的迭代器共用同一个类型，
// 差异只在 [[ArrayIterationKind]] 内部槽：产出索引、元素值，或 [index, value] 对。
//
// 关联 ADR: 016（Type 体系）、018（对象模型）

#ifndef NEO_CORE_ITERATOR_KIND_H
#define NEO_CORE_ITERATOR_KIND_H

#include <cstdint>

namespace neo::core {

// 数组迭代器种类（对应规范 [[ArrayIterationKind]] 的 key / value / key+value）
enum class ArrayIterationKind : uint8_t {
    Key,          // Array.prototype.keys —— 产出索引
    Value,        // Array.prototype.values —— 产出元素值
    KeyAndValue,  // Array.prototype.entries —— 产出 [index, value]
};

}  // namespace neo::core

#endif  // NEO_CORE_ITERATOR_KIND_H
