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
    // 箭头函数处于派生构造器的词法上下文：其 super() 需绑定外层构造器
    // 的函数环境，故必须捕获定义点运行时环境链（§14.2.16）。
    bool lexicalSuperCall = false;
    // 函数体按严格模式执行（继承外层、模块、或自身 "use strict"）。
    // Compiler 据此发射 set_strict，并决定 arguments 是否 mapped。
    bool isStrict = false;
};

}  // namespace neo::sema

#endif  // NEO_SEMA_FUNCTION_INFO_H
