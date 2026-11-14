// sema/ControlFlowPass.h - 控制流上下文早期错误
//
// 检查 Parser 无法覆盖、依赖上下文位置的早期错误：
//   - return 只能出现在函数/箭头函数内；
//   - break/continue 的循环、switch、标签匹配（不跨函数/static block 边界）；
//   - 重复标签（ContainsDuplicateLabels）；
//   - new.target 只能在函数上下文；
//   - super() 只能在派生类构造函数，super.prop 只能在方法/字段/static block；
//   - await / yield / for await 的 async / generator / module 上下文；
//   - 类字段初始化器与 static block 的 ContainsArguments。
//
// 依赖 ResolvePass 的 IdentifierBinding 标注来区分“引用”与“绑定/属性名”。
//
// 关联 ADR: 001（Sema 早期错误）、025（两遍 walk）、058（Sema Walker）

#ifndef NEO_SEMA_CONTROL_FLOW_PASS_H
#define NEO_SEMA_CONTROL_FLOW_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/FunctionInfo.h"
#include "sema/SemaContext.h"

#include <memory>

namespace neo::sema {

class IVisitor;

std::unique_ptr<IVisitor> makeControlFlowVisitor(AnnotationStore& annotations);

void runControlFlowPass(frontend::Node& program, SemaContext& context,
                        AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_CONTROL_FLOW_PASS_H
