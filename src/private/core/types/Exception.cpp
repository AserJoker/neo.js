// types/exception.cpp - ExceptionData::enumerate_children 实现
//
// 向 GC 报告持有的子 value（被抛出的错误对象）。child 永远不为 nullptr
// （不携带错误对象时用 undefined Value 表示），故无条件 visit。
//
// 关联 ADR: 015（data 持子 value 而非子 data，无环）、017（Exception 类型）

#include "core/types/Exception.h"

#include "core/IGCEngine.h"     // IGCVisitor 完整定义
#include "core/Value.h"  // Value 完整定义

namespace neo::core {

void ExceptionData::enumerate_children(IGCVisitor* visitor) {
    // child 永远不为 nullptr（不携带错误对象时用 undefined Value 表示）。
    visitor->visit(child);
}

}  // namespace neo::core
