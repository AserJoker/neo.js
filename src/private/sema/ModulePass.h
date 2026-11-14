// sema/ModulePass.h - 模块绑定与导出早期错误
//
// 检查：
//   - import/export 声明与 import.meta 仅允许出现在模块；
//   - ExportedNames 唯一（重复导出、重复 default）；
//   - export { x } 的本地绑定必须已在模块作用域声明
//     （export ... from 不检查本地绑定）；
//   - import 绑定名必须是 Identifier。
//
// 依赖 ResolvePass 的 IdentifierBinding 标注判断本地导出是否已声明。
//
// 关联 ADR: 001（Sema 早期错误）、025（两遍 walk）、058（Sema Walker）

#ifndef NEO_SEMA_MODULE_PASS_H
#define NEO_SEMA_MODULE_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

#include <memory>

namespace neo::sema {

class IVisitor;

std::unique_ptr<IVisitor> makeModuleVisitor(AnnotationStore& annotations);

void runModulePass(frontend::Node& program, SemaContext& context,
                   AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_MODULE_PASS_H
