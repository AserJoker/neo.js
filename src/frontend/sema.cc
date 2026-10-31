/*
 * 前端：sema 独立阶段实现（AST 后、compile 前，scope-redesign.md §10.8）。
 *
 * 职责：
 *   - 遍历 AST，模拟作用域创建/销毁（shadow_scope 树）。
 *   - 变量定义提升：
 *     - var → 最近函数/模块级 shadow scope（Program/Module/函数 body scope）
 *     - let/const → 当前（块）shadow scope
 *   - 标识符解析：沿 shadow 链查找定义点，挂到 identifier 节点
 *     （node->resolved；穿越函数 scope → 名字加入函数捕获列表）。
 *
 * 纯编译期：shadow 产物只给 compiler 参考，不流到运行期。
 */

#include "neojs/frontend/sema.h"

namespace neojs {
namespace sema {

namespace {

using neojs::NodeType;
using neojs::VarKind;

struct Analyzer;
void analyze_node(Analyzer &a, ASTNode *node);
void analyze_expr(Analyzer &a, ASTNode *node);
void analyze_function(Analyzer &a, ASTNode *node);

/* sema 遍历器：持 shadow scope 栈 + 当前函数级 scope。 */
struct Analyzer {
  std::vector<ShadowScope *> stack; /* shadow scope 栈（栈顶=当前） */

  ShadowScope *cur() const { return stack.back(); }

  /* 新建 shadow scope 并入栈。is_function：函数级（var 提升目标）。 */
  ShadowScope *push_scope(bool is_function) {
    ShadowScope *s = new ShadowScope();
    s->parent = cur();
    s->is_function = is_function;
    if (s->parent != nullptr) s->parent->children.push_back(s);
    stack.push_back(s);
    return s;
  }
  void pop_scope() { stack.pop_back(); }

  /* 找最近函数/模块级 scope（var 提升目标）。 */
  ShadowScope *function_scope() {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if ((*it)->is_function) return *it;
    }
    return stack.front(); /* 兜底：顶层 */
  }

  /* 当前 scope 定义变量（let/const → cur；var → function_scope）。 */
  ShadowValue *define(const std::string &name, VarKind kind, ASTNode *decl) {
    ShadowScope *target = (kind == VarKind::Var) ? function_scope() : cur();
    return define_in(target, name, kind, decl);
  }

  /* 在指定 scope 定义变量（同名覆盖）。 */
  ShadowValue *define_in(ShadowScope *target, const std::string &name,
                         VarKind kind, ASTNode *decl) {
    ShadowValue *v = new ShadowValue();
    v->name = name;
    v->kind = kind;
    v->decl = decl;
    target->values.push_back(v);
    target->named[name] = v;
    return v;
  }

  /* 解析标识符：沿 shadow 链找定义。返回找到的 ShadowValue*，未找到 nullptr。
   * 穿越函数 scope 时，把名字加入最近函数节点的捕获列表（§10.8）。 */
  ShadowValue *resolve(const std::string &name, ASTNode *id) {
    ShadowScope *found_scope = nullptr;
    ShadowValue *found = nullptr;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      ShadowScope *s = *it;
      auto f = s->named.find(name);
      if (f != s->named.end()) {
        found_scope = s;
        found = f->second;
        break;
      }
    }
    if (found == nullptr) {
      id->resolved = nullptr; /* 全局 */
      return nullptr;
    }
    id->resolved = found;

    /* 穿越函数 scope：从 found_scope 到当前 scope 之间，若跨过**其他函数**
     * 的函数级 scope（非当前函数自身），名字加入最近函数节点捕获列表
     * （§10.8 逐层捕获传递）。
     * 注意：形参定义在 argument scope（非函数级），函数体在 body scope
     * （函数级）。若不加区分，函数体内引用本函数形参也会被误判为跨函数
     * 捕获（body scope 是函数级且位于 found_scope=arg_scope 之上）。
     * 判定口径：先找当前函数节点（栈顶最近 is_function 的 node），
     * 只有跨过 node != 当前函数的函数级 scope 才算真捕获。 */
    ASTNode *cur_fn = nullptr;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      if ((*it)->is_function && (*it)->node != nullptr) {
        cur_fn = (*it)->node;
        break;
      }
    }
    bool crossed = false;
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
      ShadowScope *s = *it;
      /* 先判跨函数再判到达：found_scope 本身是其他函数的函数级 scope
       * （外层函数体）时，也要计为跨函数捕获。 */
      if (s->is_function && s->node != cur_fn) {
        crossed = true;
        break;
      }
      if (s == found_scope) break;
    }
    if (crossed) {
      for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        ShadowScope *s = *it;
        if (s->is_function && s->node != nullptr) {
          add_capture(s->node, name);
          break;
        }
      }
    }
    return found;
  }

  /* 把名字加入函数节点捕获列表（去重）。 */
  void add_capture(ASTNode *fn, const std::string &name) {
    for (const std::string &c : fn->captures) {
      if (c == name) return;
    }
    fn->captures.push_back(name);
  }
};

/* 表达式遍历：标识符解析 + 子表达式递归（不含作用域变更）。 */
void analyze_expr(Analyzer &a, ASTNode *node) {
  if (node == nullptr) return;
  switch (node->type) {
    case NodeType::Identifier: {
      a.resolve(node->name, node);
      return;
    }
    case NodeType::Literal:
    case NodeType::ThisExpression:
      return;
    case NodeType::ArrowFunctionExpression:
      /* 箭头函数：函数作用域（参数 + body）。 */
      analyze_function(a, node);
      return;
    case NodeType::Assignment: {
      /* lhs 是 Identifier 时解析；否则（成员等）递归。 */
      if (!node->children.empty() && node->children[0] != nullptr &&
          node->children[0]->type == NodeType::Identifier) {
        a.resolve(node->children[0]->name, node->children[0]);
      } else if (!node->children.empty()) {
        analyze_expr(a, node->children[0]);
      }
      if (node->children.size() > 1) analyze_expr(a, node->children[1]);
      return;
    }
    case NodeType::CallExpression:
    case NodeType::MemberExpression:
    case NodeType::NewExpression:
    case NodeType::UnaryOp:
    case NodeType::BinaryOp:
    case NodeType::Conditional:
    case NodeType::Sequence:
      for (ASTNode *c : node->children) analyze_expr(a, c);
      return;
    default:
      /* 其余表达式节点（对象/数组字面量等）：递归子节点。 */
      for (ASTNode *c : node->children) analyze_expr(a, c);
      return;
  }
}

/* 函数节点（FunctionDeclaration/Expression/ArrowFunction）：
 * 两个 shadow scope：argument（参数+this，非函数级）+ body（函数级，var 提升）。 */
void analyze_function(Analyzer &a, ASTNode *node) {
  /* 函数名：声明 → 外层定义；表达式 → 内部自引用。 */
  size_t param_start = 0;
  if (node->fn_has_name && !node->children.empty()) {
    ASTNode *name_node = node->children[0];
    if (name_node->type == NodeType::Identifier) {
      if (node->type == NodeType::FunctionDeclaration) {
        /* 函数声明：名字定义在外层（var 提升到外层函数级）。 */
        a.define(name_node->name, VarKind::Var, name_node);
      } else {
        /* 函数表达式：内部名自引用，定义在 argument scope。 */
      }
      param_start = 1;
    }
  }
  ASTNode *body = node->children.empty() ? nullptr : node->children.back();

  /* argument scope（参数 + this，非函数级） */
  ShadowScope *arg_scope = a.push_scope(false);
  arg_scope->node = node;
  for (size_t i = param_start; i + 1 < node->children.size(); ++i) {
    ASTNode *param = node->children[i];
    ASTNode *id = param;
    if (param->type == NodeType::AssignmentPattern && !param->children.empty()) {
      id = param->children[0]; /* 默认值 a = 1 → 取 a */
    }
    if (id->type == NodeType::Identifier) {
      a.define_in(arg_scope, id->name, VarKind::Let, id);
    }
    /* 解构模式 M5-B 不做 */
  }
  /* this：M5-B 不做（调用时用全局 undefined 顶替） */

  /* body scope（函数级，var 提升目标） */
  ShadowScope *body_scope = a.push_scope(true);
  body_scope->node = node;
  node->scope_info = body_scope; /* 挂主 scope（compiler 读 var 提升清单） */
  if (body != nullptr) {
    if (body->type == NodeType::BlockStatement) {
      /* 函数体块不建独立 shadow scope：body 语句直接在 body scope 内分析
       * （函数体块是函数级作用域本身，var 提升/let TDZ 都落在 body scope） */
      for (ASTNode *c : body->children) analyze_node(a, c);
    } else {
      analyze_node(a, body); /* 箭头函数表达式体 */
    }
  }
  a.pop_scope();
  a.pop_scope();
}

/* 块语句：新 shadow scope（非函数级），let/const 定义在块内。 */
void analyze_block(Analyzer &a, ASTNode *node) {
  ShadowScope *s = a.push_scope(false);
  s->node = node;
  node->scope_info = s; /* 挂主 scope */
  for (ASTNode *c : node->children) analyze_node(a, c);
  a.pop_scope();
}

/* 声明语句：var/let/const。var → 函数级，let/const → 当前块。 */
void analyze_decl(Analyzer &a, ASTNode *node) {
  for (ASTNode *d : node->children) {
    if (d->type != NodeType::VariableDeclarator) continue;
    if (d->children.empty()) continue;
    ASTNode *id = d->children[0];
    if (id->type == NodeType::Identifier) {
      a.define(id->name, node->varkind, d);
    }
    /* 解构 M5-B 不做 */
    if (d->children.size() > 1) analyze_expr(a, d->children[1]); /* init */
  }
}

/* 语句遍历（分派）。 */
void analyze_node(Analyzer &a, ASTNode *node) {
  if (node == nullptr) return;
  switch (node->type) {
    case NodeType::Program:
    case NodeType::Module: {
      ShadowScope *s = a.push_scope(true); /* 顶层=函数级（var 提升目标） */
      s->node = node;
      node->scope_info = s; /* 挂主 scope */
      for (ASTNode *c : node->children) analyze_node(a, c);
      a.pop_scope();
      return;
    }
    case NodeType::BlockStatement:
      analyze_block(a, node);
      return;
    case NodeType::VariableDeclaration:
      analyze_decl(a, node);
      return;
    case NodeType::FunctionDeclaration:
    case NodeType::FunctionExpression:
      analyze_function(a, node);
      return;
    case NodeType::ExprStmt:
      if (!node->children.empty()) analyze_expr(a, node->children[0]);
      return;
    case NodeType::IfStatement:
    case NodeType::WhileStatement:
    case NodeType::DoWhileStatement:
      for (ASTNode *c : node->children) analyze_node(a, c);
      return;
    case NodeType::ForStatement: {
      /* for-init / for-body 两个独立 shadow scope（§10.8）。 */
      if (node->children.size() < 4) return;
      ShadowScope *init_scope = a.push_scope(false);
      init_scope->node = node;
      node->scope_info = init_scope; /* 挂主 scope（for-init） */
      ASTNode *init = node->children[0];
      if (init != nullptr) {
        if (init->type == NodeType::VariableDeclaration) {
          analyze_decl(a, init);
        } else {
          analyze_expr(a, init);
        }
      }
      if (node->children[1] != nullptr) analyze_expr(a, node->children[1]);
      /* body：独立 scope（每次迭代重建，per-iteration 闭包） */
      ShadowScope *body_scope = a.push_scope(false);
      body_scope->node = node;
      if (node->children[3] != nullptr) analyze_node(a, node->children[3]);
      a.pop_scope();
      a.pop_scope();
      if (node->children[2] != nullptr) analyze_expr(a, node->children[2]);
      return;
    }
    case NodeType::ReturnStatement:
    case NodeType::ThrowStatement:
      if (!node->children.empty()) analyze_expr(a, node->children[0]);
      return;
    default:
      /* 其他语句（break/continue/try/with/class 等）：递归子节点 */
      for (ASTNode *c : node->children) analyze_node(a, c);
      return;
  }
}

}  // namespace

/* 遍历 AST，执行 sema。 */
bool analyze(ASTNode *root, Result &out) {
  if (root == nullptr) return false;
  Analyzer a;
  /* 虚拟根：Program 挂其下。 */
  ShadowScope *root_scope = new ShadowScope();
  root_scope->is_function = true;
  out.root = root_scope;
  a.stack.push_back(root_scope);
  analyze_node(a, root);
  a.stack.pop_back();
  return true;
}

}  // namespace sema
}  // namespace neojs
