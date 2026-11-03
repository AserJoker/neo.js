// data.cpp - Data 子类实现
//
// 本阶段只有 ExceptionData::enumerate_children 需要非内联实现（覆写基类默认空方法，
// 向 visitor 报告持有的子 value，让 GC 沿引用链标记）。其余 Data 子类全部内联在
// impl_data.h 中。
//
// 关联 ADR: 015（data 引用计数分层，data 持子 value 而非子 data，无环）、017（Exception 类型）

#include "core/impl_data.h"

#include "core/gc.h"    // GCVisitor 完整定义
#include "core/value.h"  // Value 完整定义（enumerate_children 调 visitor->visit）

namespace neo::core {

void ExceptionData::enumerate_children(GCVisitor* visitor) {
    // 向 GC 报告持有的子 value（被抛出的错误对象）。
    // child 永远不为 nullptr（不携带错误对象时用 undefined Value 表示，而非空指针）。
    visitor->visit(child);
}

}  // namespace neo::core
