// sema/FunctionInfo.h - 函数级标注
//
// ResolvePass 写入 hasDirectEval；ControlFlowPass 补齐函数种类、方法/
// 构造函数与 static block 等 AST 上无法单独判定的上下文信息。
// 标注随 SemaResult 输出给 Compiler。

#ifndef NEO_SEMA_FUNCTION_INFO_H
#define NEO_SEMA_FUNCTION_INFO_H

#include "sema/AnnotationStore.h"

namespace neo::sema {

struct FunctionInfo final : IMeta {
    bool isArrow = false;
    bool isAsync = false;
    bool isGenerator = false;
    bool isMethod = false;
    bool isConstructor = false;
    bool isDerivedConstructor = false;
    bool isStaticBlock = false;
    bool hasDirectEval = false;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_FUNCTION_INFO_H
