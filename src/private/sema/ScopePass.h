// sema/ScopePass.h - 第一遍 walk：作用域树与变量提升
//
// 按 ADR-025 建立 shadow_scope 树：
//   - Program 是 var/lexical 根作用域；
//   - 函数拆成 closure / arguments / body 三层；
//   - block / for / catch / switch / class / with 各自建作用域；
//   - var 提升到最近的 function/module/static-block 作用域；
//   - let/const/class/import 留在最近的 block 作用域。
//
// ScopeGraph 是 Sema 内部 scratch，不进 Compiler；ResolvePass 通过
// NodeId → ShadowScope 的映射复用这棵树。
//
// 关联 ADR: 025（两遍 walk）、058（Sema Walker）

#ifndef NEO_SEMA_SCOPE_PASS_H
#define NEO_SEMA_SCOPE_PASS_H

#include "frontend/Node.h"
#include "sema/SemaContext.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neo::sema {

enum class ShadowScopeKind : uint8_t {
    Program,
    FunctionClosure,
    FunctionArguments,
    FunctionBody,
    Block,
    For,
    Catch,
    Class,
    With,
    StaticBlock,
};

enum class ShadowBindingKind : uint8_t {
    Var,
    Let,
    Const,
    Function,
    Class,
    Param,
    Arguments,
    Import,
    CatchParam,
    FunctionName,
    ClassName,
    // 块级 / 模块级函数声明：lexical 绑定，与 var 型 Function 区分。
    LexicalFunction,
};

struct ShadowBinding {
    std::string name;
    ShadowBindingKind kind;
    bool mutableBinding;
    bool initialized;  // false 表示 TDZ
    frontend::SourceRange range;
};

struct ShadowScope {
    ShadowScope(ShadowScopeKind kind, ShadowScope* parent)
        : kind(kind), parent(parent) {}

    ShadowScopeKind kind;
    ShadowScope* parent;
    std::vector<std::unique_ptr<ShadowScope>> children;
    std::unordered_map<std::string, ShadowBinding> bindings;
    // 本作用域语法范围内出现的 var 名字（用于 var/lexical 冲突检查）。
    std::unordered_set<std::string> varNames;
    // catch 作用域：CatchParameter 是否为简单 BindingIdentifier。
    // 仅简单参数允许 Annex B B.3.4 的同名 var 绑定。
    bool simpleCatchParameter = true;

    ShadowBinding* findLocal(std::string_view name) {
        const auto it = bindings.find(std::string(name));
        return it == bindings.end() ? nullptr : &it->second;
    }
};

struct FunctionScopes {
    ShadowScope* closure = nullptr;
    ShadowScope* arguments = nullptr;
    ShadowScope* body = nullptr;
};

struct ScopeGraph {
    std::unique_ptr<ShadowScope> root;
    std::unordered_map<frontend::NodeId, ShadowScope*> scopes;
    std::unordered_map<frontend::NodeId, FunctionScopes> functions;

    ShadowScope* findScope(frontend::NodeId id) const {
        const auto it = scopes.find(id);
        return it == scopes.end() ? nullptr : it->second;
    }
};

ScopeGraph runScopePass(frontend::Node& program, SemaContext& context);

}  // namespace neo::sema

#endif  // NEO_SEMA_SCOPE_PASS_H
