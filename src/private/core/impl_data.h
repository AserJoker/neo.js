// impl_data.h (private) - 具体 Data 子类声明
//
// 本头仅在 core 模块内部可见（src/private/）。每种类型一个 Data 子类承载负载。
// Data 内存走 Allocator（由 GC 的 alloc_data<T> 分配），GC sweep 在 ref 归 0 时释放。
//
// 本阶段类型清单（ADR-017）：
//   undefined/null —— 空负载（仅占类型标识）
//   boolean        —— bool
//   number         —— double
//   string         —— std::string（不可变：构造后不再修改，"修改"操作返回新 StringData）
//   exception      —— Value*（被抛出的错误对象，本阶段唯一复杂类型，GC 须介入）
//   fatal          —— const char*（字面量字符串，静态存储期，构造时确定，不经 Allocator）
//
// ExceptionData 持 Value*（GC 管的对象），须覆写 enumerate_children 让 GC 沿引用链
// 标记被引用的错误对象（ADR-015 data 层持子 value 而非子 data，无环）。
// FatalData 持 const char*（字面量），非 GC 管的对象，enumerate_children 默认空。
//
// 关联 ADR: 015（data 引用计数分层）、016（Type 体系）、017（类型清单）、029（生命周期）

#ifndef NEO_CORE_PRIVATE_IMPL_DATA_H
#define NEO_CORE_PRIVATE_IMPL_DATA_H

#include "core/data.h"

#include <string>
#include <string_view>

namespace neo::core {

// 前向声明
class Value;

// undefined：空负载
class UndefinedData : public Data {};

// null：空负载
class NullData : public Data {};

// boolean：持 bool
class BooleanData : public Data {
public:
    bool value;
    explicit BooleanData(bool v) : value(v) {}
};

// number：持 double
class NumberData : public Data {
public:
    double value;
    explicit NumberData(double v) : value(v) {}
};

// string：持 std::string，不可变（构造后不再修改）
// 任何"修改"操作（concat/replace/...）返回新 StringData。
class StringData : public Data {
public:
    std::string value;
    explicit StringData(std::string v) : value(std::move(v)) {}
    explicit StringData(std::string_view v) : value(v) {}
};

// exception：持 Value*（被抛出的错误对象）
// 本阶段唯一复杂类型：data 持 GC 管的子 value，须覆写 enumerate_children 让 GC 标记。
// child_value_ 是 GC 管的对象，其生命周期由 GC 决定；ExceptionData 仅持有引用（ref 不计，
// 因为是 Value* 而非 Data*——GC 跟踪 Value，data->enumerate_children 报告引用关系）。
// **child 永远不为 nullptr**：不携带错误对象时用 undefined Value 表示，而非空指针。
class ExceptionData : public Data {
public:
    Value* child;  // 被抛出的错误对象（GC 管的 Value*，永远不为 nullptr）
    explicit ExceptionData(Value* c) : child(c) {}

    // 覆写：向 visitor 报告持有的子 value，让 GC 沿引用链标记
    void enumerate_children(GCVisitor* visitor) override;
};

// fatal：持 const char*（字面量字符串，静态存储期）
// 构造时确定，硬编码（如 "OOM"）。不经 Allocator 分配，GC 无须跟踪。
// FatalData 对象本身仍经 alloc_data 分配（Data 基类部分），但字符串负载是字面量。
class FatalData : public Data {
public:
    const char* message;  // 字面量字符串，静态存储期，无须释放
    explicit FatalData(const char* msg) : message(msg) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_IMPL_DATA_H
