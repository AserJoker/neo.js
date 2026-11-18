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

// Iterator.prototype 惰性辅助方法种类（ES2025 §27.1.3.3）。
enum class IteratorHelperKind : uint8_t {
    Map,
    Filter,
    Take,
    Drop,
    FlatMap,
};

// 集合种类（Map/Set/WeakMap/WeakSet 共用一套对象布局与类型）。
enum class CollectionKind : uint8_t { Map, Set, WeakMap, WeakSet };

// 集合迭代器产出种类（ES2024 §24.1.5 / §24.2.5）。
enum class CollectionIterationKind : uint8_t { Key, Value, KeyAndValue };

// TypedArray 元素类型（ES2024 §25.2.1）。
enum class TypedArrayKind : uint8_t {
    Int8,
    Uint8,
    Uint8Clamped,
    Int16,
    Uint16,
    Int32,
    Uint32,
    Float32,
    Float64,
    BigInt64,
    BigUint64,
};

}  // namespace neo::core

#endif  // NEO_CORE_ITERATOR_KIND_H
