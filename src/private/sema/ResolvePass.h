// sema/ResolvePass.h - 第二遍 walk：标识符解析与闭包穿透捕获
//
// 对每个标识符引用沿 shadow_scope 链向上查找：
//   - 同函数内找到 → Local；
//   - 跨函数边界找到 → Captured，并在被穿越的每个函数/字段初始化器上
//     登记捕获名字（闭包穿透）；
//   - 未找到 → Script 标 Global，Module 标 Unresolved（均为运行时全局查找，
//     区别在于 Module 不做隐式全局创建）。
//
// 捕获登记是“查询路径的自然结果”，不是编译期向父函数传播列表。
// 直接 eval 会破坏静态捕获的完备性，因此在所属函数上标记 hasDirectEval，
// 供 Compiler/Runtime 回退 Environment 链（ADR-025）。
//
// 关联 ADR: 001（Sema 标注）、013（闭包作用域）、014（TDZ）、025（两遍 walk）、
//           058（Sema Walker）

#ifndef NEO_SEMA_RESOLVE_PASS_H
#define NEO_SEMA_RESOLVE_PASS_H

#include "frontend/Node.h"
#include "sema/AnnotationStore.h"
#include "sema/FunctionInfo.h"
#include "sema/ScopePass.h"
#include "sema/SemaContext.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neo::sema {

enum class BindingLocation : uint8_t {
    Local,       // 同一函数（或 Program）内
    Captured,    // 跨函数边界
    Global,      // Script 未解析：运行时全局查找
    Unresolved,  // Module 未解析：运行时全局查找，无隐式全局创建
};

// 标识符引用的解析结果。binding 指向 ScopeGraph 中稳定的 ShadowBinding。
struct IdentifierBinding final : IMeta {
    IdentifierBinding(ShadowScope* scope, ShadowBinding* binding,
                      BindingLocation location)
        : scope(scope), binding(binding), location(location) {}

    ShadowScope* scope = nullptr;
    ShadowBinding* binding = nullptr;
    BindingLocation location = BindingLocation::Unresolved;
};

// 函数/字段初始化器的显式捕获列表（去重、按名字排序）。
struct FunctionCaptures final : IMeta {
    std::vector<std::string> names;
};

void runResolvePass(frontend::Node& program, ScopeGraph& graph,
                    SemaContext& context, AnnotationStore& annotations);

}  // namespace neo::sema

#endif  // NEO_SEMA_RESOLVE_PASS_H
