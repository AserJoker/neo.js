// sema/AnnotatePass.h - 最后一遍 walk：产出 Compiler 消费的函数级标注
//
// ResolvePass 已经产出标识符绑定与捕获列表；本 pass 汇总函数/字段初始化器
// 在 AST 上无法单独判定的上下文信息：
//   - 函数种类（普通函数 / 箭头 / 方法 / 构造函数 / static block）；
//   - 是否派生类构造函数（需要类是否 extends）；
//   - 直接 eval 标记（从 ResolvePass 的 FunctionFlags 复制）。
//
// 标注按节点 id 存放，随 SemaResult 输出给 Compiler。

#ifndef NEO_SEMA_ANNOTATE_PASS_H
#define NEO_SEMA_ANNOTATE_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/SemaContext.h"

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

void runAnnotatePass(frontend::Node& program, SemaContext& context,
                     AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_ANNOTATE_PASS_H
