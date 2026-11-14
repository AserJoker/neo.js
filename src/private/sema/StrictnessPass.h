// sema/StrictnessPass.h - 严格模式早期错误检查
//
// 严格性作用域（ADR-057）：
//   Program(script)        → sloppy
//   Program(module)        → strict
//   Function/Arrow/Method  → 继承外层 + 自身 directive prologue
//   ClassBody              → 恒 strict
//
// 本 pass 只读 AST，不修改节点；错误写入 SemaContext。
//
// 关联 ADR: 057（严格模式在 Sema）、058（Sema Walker）

#ifndef NEO_SEMA_STRICTNESS_PASS_H
#define NEO_SEMA_STRICTNESS_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

#include <memory>

namespace neo::sema {

class IVisitor;

// 供 EarlyErrorPass 与独立 pass 共用同一个 visitor 实现。
std::unique_ptr<IVisitor> makeStrictnessVisitor();

void runStrictnessPass(frontend::Node& program, SemaContext& context,
                       AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_STRICTNESS_PASS_H
