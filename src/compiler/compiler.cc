/*
 * 内部 C++ 实现：Compiler（AST → 字节码）。
 *
 * M2.4 ① 编译器骨架 + 表达式编译。
 *   - 表达式：字面量 / 标识符 / 一元 / 二元 / 逻辑短路 / 条件 / 赋值 /
 *     调用 / 序列。
 *   - 语句：表达式语句 + 变量声明（简单标识符；解构在②③扩展）。
 *     控制流（if/while/for/return/块）与函数编译在 M2.4 ②③。
 */

#include "neojs/compiler/compiler.h"

#include <utility>

#include "neojs/frontend/ast.h"
#include "neojs/runtime/conversion.h"

namespace neojs {

namespace {

/* 二元运算符 -> 指令。返回 false 表示不支持（逻辑短路/特殊运算符单独处理）。 */
bool binaryOpcode(BinaryOpKind op, Opcode *out) {
  switch (op) {
    case BinaryOpKind::Add: *out = Opcode::Add; return true;
    case BinaryOpKind::Sub: *out = Opcode::Sub; return true;
    case BinaryOpKind::Mul: *out = Opcode::Mul; return true;
    case BinaryOpKind::Div: *out = Opcode::Div; return true;
    case BinaryOpKind::Mod: *out = Opcode::Mod; return true;
    case BinaryOpKind::Pow: *out = Opcode::Pow; return true;
    case BinaryOpKind::LShift: *out = Opcode::LShift; return true;
    case BinaryOpKind::RShift: *out = Opcode::RShift; return true;
    case BinaryOpKind::URShift: *out = Opcode::URShift; return true;
    case BinaryOpKind::Lt: *out = Opcode::Lt; return true;
    case BinaryOpKind::Le: *out = Opcode::Le; return true;
    case BinaryOpKind::Gt: *out = Opcode::Gt; return true;
    case BinaryOpKind::Ge: *out = Opcode::Ge; return true;
    case BinaryOpKind::Eq: *out = Opcode::Eq; return true;
    case BinaryOpKind::Ne: *out = Opcode::Ne; return true;
    case BinaryOpKind::StrictEq: *out = Opcode::StrictEq; return true;
    case BinaryOpKind::StrictNe: *out = Opcode::StrictNe; return true;
    case BinaryOpKind::BitAnd: *out = Opcode::BitAnd; return true;
    case BinaryOpKind::BitXor: *out = Opcode::BitXor; return true;
    case BinaryOpKind::BitOr: *out = Opcode::BitOr; return true;
    default: return false;
  }
}

/* 复合赋值运算符 -> 对应二元运算符。 */
bool compoundAssignToBinary(AssignmentOpKind aop, BinaryOpKind *bop) {
  switch (aop) {
    case AssignmentOpKind::AddAssign: *bop = BinaryOpKind::Add; return true;
    case AssignmentOpKind::SubAssign: *bop = BinaryOpKind::Sub; return true;
    case AssignmentOpKind::MulAssign: *bop = BinaryOpKind::Mul; return true;
    case AssignmentOpKind::DivAssign: *bop = BinaryOpKind::Div; return true;
    case AssignmentOpKind::ModAssign: *bop = BinaryOpKind::Mod; return true;
    case AssignmentOpKind::PowAssign: *bop = BinaryOpKind::Pow; return true;
    case AssignmentOpKind::LShiftAssign: *bop = BinaryOpKind::LShift; return true;
    case AssignmentOpKind::RShiftAssign: *bop = BinaryOpKind::RShift; return true;
    case AssignmentOpKind::URShiftAssign: *bop = BinaryOpKind::URShift; return true;
    case AssignmentOpKind::BitAndAssign: *bop = BinaryOpKind::BitAnd; return true;
    case AssignmentOpKind::BitXorAssign: *bop = BinaryOpKind::BitXor; return true;
    case AssignmentOpKind::BitOrAssign: *bop = BinaryOpKind::BitOr; return true;
    default: return false;
  }
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 顶层编译                                                                    */
/* -------------------------------------------------------------------------- */

CompileResult Compiler::compile(const AST &ast) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  for (CompilerScope *s : scopes_) delete s;
  scopes_.clear();
  scope_ = nullptr;
  builder_ = BytecodeBuilder();

  /* 顶层作用域帧。 */
  CompilerScope *top = new CompilerScope;
  scopes_.push_back(top);
  scope_ = top;

  if (ast.root != nullptr) {
    const auto &stmts = ast.root->children;
    if (!stmts.empty()) {
      /* 除最后一条外：逐条编译（语句自身管理栈平衡，值 Pop）。
       * FunctionDeclaration 已在顶层收集阶段实例化挂到 global，此处跳过。 */
      for (size_t i = 0; i + 1 < stmts.size(); ++i) {
        if (stmts[i] == nullptr) continue;
        if (stmts[i]->type == NodeType::FunctionDeclaration) continue;
        if (!compileStmt(*stmts[i])) return failResult();
      }
      /* 最后一条：若是表达式语句，其值作为 completion value（不 Pop）；
       * FunctionDeclaration 已单独实例化（completion value = undefined）；
       * 否则编译后补 undefined。 */
      const ASTNode *last = stmts.back();
      if (last != nullptr && last->type == NodeType::ExprStmt &&
          !last->children.empty()) {
        if (!compileExpr(*last->children[0])) return failResult();
        builder_.ret(); /* 栈顶即 completion value */
      } else {
        if (last != nullptr && last->type != NodeType::FunctionDeclaration &&
            !compileStmt(*last))
          return failResult();
        builder_.push_op(Opcode::PushUndefined);
        builder_.ret();
      }
    } else {
      /* 空程序：completion value = undefined。 */
      builder_.push_op(Opcode::PushUndefined);
      builder_.ret();
    }
  } else {
    /* 空程序：completion value = undefined。 */
    builder_.push_op(Opcode::PushUndefined);
    builder_.ret();
  }

  /* 局部变量槽数（含临时槽）写入字节码。 */
  builder_.set_num_locals(scope_->next_slot);
  result.ok = true;
  result.fn = builder_.build();
  return result;
}

CompileResult Compiler::compileFunction(const ASTNode &fn) {
  CompileResult result;
  failed_ = false;
  error_.clear();
  for (CompilerScope *s : scopes_) delete s;
  scopes_.clear();
  scope_ = nullptr;

  /* 函数名（可空）：fn_has_name 标记 children[0] 是否为函数名。 */
  std::string fn_name;
  if (fn.fn_has_name && !fn.children.empty()) {
    fn_name = fn.children[0]->name;
  }
  builder_ = BytecodeBuilder(fn_name);

  /* 顶层作用域帧（单函数编译：无外层，捕获解析到全局）。 */
  CompilerScope *top = new CompilerScope;
  scopes_.push_back(top);
  scope_ = top;

  if (!compileFunctionBody(fn)) return failResult();

  builder_.set_num_locals(scope_->next_slot);
  result.ok = true;
  result.fn = builder_.build();
  return result;
}

/* 函数体编译共享实现：解析参数（入本函数作用域帧）+ 编译 body。
 * 调用方已建立本函数作用域帧（scope_ 指向本函数）。 */
bool Compiler::compileFunctionBody(const ASTNode &fn) {
  /* 布局：children[start..n-2] = 参数，children[n-1] = body。
   * fn_has_name = true 时 children[0] 是函数名，start = 1；否则 start = 0。 */
  size_t start = fn.fn_has_name ? 1 : 0;
  size_t body_index = fn.children.size() - 1;

  /* 参数槽分配（M5.1a）：每个参数占用一个源槽（顺序入槽，VM 按位置
   * 复制实参 args[i] -> 槽 i）。
   *   Identifier        -> 该槽即绑定
   *   AssignmentPattern -> 源槽 = 绑定槽（undefined 时求默认值）
   *   RestElement       -> 源槽 = rest 数组（RestParam 收集）
   *   解构模式          -> 源槽（无名）+ 内部绑定槽（declarePatternBindings）
   * 两遍：先声明全部绑定（槽位稳定），再发射入口初始化代码。 */
  struct ParamInfo {
    const ASTNode *node = nullptr;
    uint16_t slot = 0; /* 源槽 */
  };
  std::vector<ParamInfo> params;
  for (size_t i = start; i < body_index; ++i) {
    const ASTNode *p = fn.children[i];
    ParamInfo info;
    info.node = p;
    if (p->type == NodeType::Identifier) {
      info.slot = declareLocal(p->name);
    } else if (p->type == NodeType::AssignmentPattern) {
      if (p->children.empty()) {
        fail("invalid default parameter");
        return false;
      }
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        info.slot = declareLocal(target->name);
      } else {
        /* 解构 + 默认值：源槽 = 无名临时槽，内部绑定预声明。 */
        info.slot = scope_->next_slot++;
        if (!declarePatternBindings(*target)) {
          fail("invalid destructuring default parameter");
          return false;
        }
      }
    } else if (p->type == NodeType::RestElement) {
      if (p->children.empty()) {
        fail("invalid rest parameter");
        return false;
      }
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        info.slot = declareLocal(target->name);
      } else {
        info.slot = scope_->next_slot++;
        if (!declarePatternBindings(*target)) {
          fail("invalid rest parameter");
          return false;
        }
      }
    } else if (p->type == NodeType::ObjectPattern ||
               p->type == NodeType::ArrayPattern) {
      info.slot = scope_->next_slot++;
      if (!declarePatternBindings(*p)) {
        fail("invalid destructuring parameter");
        return false;
      }
    } else {
      fail("invalid parameter");
      return false;
    }
    params.push_back(info);
  }

  /* 入口初始化（按参数顺序）：默认值 / rest / 解构。 */
  for (size_t i = 0; i < params.size(); ++i) {
    const ParamInfo &info = params[i];
    const ASTNode *p = info.node;
    if (p->type == NodeType::Identifier) continue;
    if (p->type == NodeType::AssignmentPattern) {
      const ASTNode *target = p->children[0];
      if (target->type == NodeType::Identifier) {
        /* x = default：x 为 undefined 时求默认值（§9.2.12 步骤 20）。
         *   [slot] JumpIfUndefined use_default; Jump end;
         *   use_default: [default]; end: StoreVar slot */
        BcLabel use_default = builder_.new_label();
        BcLabel end = builder_.new_label();
        builder_.load_var(info.slot);
        builder_.jump_if_undefined(&use_default);
        builder_.jump(&end);
        builder_.bind_label(use_default);
        if (!compileExpr(*p->children[1])) return false;
        builder_.bind_label(end);
        builder_.store_var(info.slot);
      } else {
        /* 解构 + 默认值：源 undefined 时先求默认值存入源槽，再解构。 */
        BcLabel use_default = builder_.new_label();
        BcLabel end = builder_.new_label();
        builder_.load_var(info.slot);
        builder_.jump_if_undefined(&use_default);
        builder_.jump(&end);
        builder_.bind_label(use_default);
        if (!compileExpr(*p->children[1])) return false;
        builder_.bind_label(end);
        builder_.store_var(info.slot);
        if (!compilePattern(*target, info.slot, /*is_declaration=*/true)) {
          return false;
        }
      }
    } else if (p->type == NodeType::RestElement) {
      /* ...rest：RestParam {i} 收集 args[i..] 为数组，存源槽。 */
      builder_.rest_param(static_cast<uint16_t>(i));
      builder_.store_var(info.slot);
      const ASTNode *target = p->children[0];
      if (target->type != NodeType::Identifier) {
        if (!compilePattern(*target, info.slot, /*is_declaration=*/true)) {
          return false;
        }
      }
    } else {
      /* 解构参数：从源槽解构绑定（f({a}) / f([x])）。 */
      if (!compilePattern(*p, info.slot, /*is_declaration=*/true)) {
        return false;
      }
    }
  }

  /* 函数体：块语句逐条编译，或箭头函数表达式体（求值后 Ret）。 */
  const ASTNode *body = fn.children[body_index];
  if (body->type == NodeType::BlockStatement) {
    for (const ASTNode *stmt : body->children) {
      if (stmt == nullptr) continue;
      if (!compileStmt(*stmt)) return false;
    }
  } else {
    /* 箭头函数表达式体：求值后作为返回值。 */
    if (!compileExpr(*body)) return false;
    builder_.ret();
  }

  /* 函数默认返回 undefined（块体自然结束）。 */
  builder_.push_op(Opcode::PushUndefined);
  builder_.ret();
  return true;
}

/* 编译子函数到独立 builder（共享参数/body 解析）。
 * 调用方负责保存/恢复外层 builder_ 与作用域帧。 */
bool Compiler::compileChildFunction(const ASTNode &fn, FunctionBytecode *out) {
  /* 函数名（可空）：fn_has_name 标记 children[0] 是否为函数名。 */
  std::string fn_name;
  if (fn.fn_has_name && !fn.children.empty()) {
    fn_name = fn.children[0]->name;
  }

  /* 建立子函数作用域帧：parent = 当前（捕获解析到外层）。 */
  CompilerScope *child = new CompilerScope;
  child->parent = scope_;
  scopes_.push_back(child);
  scope_ = child;

  /* 子函数自己的 builder（与当前 builder_ 分离）。 */
  BytecodeBuilder saved_builder;
  saved_builder = std::move(builder_); /* 保存外层 builder */
  builder_ = BytecodeBuilder(fn_name);

  bool ok = compileFunctionBody(fn);

  /* 恢复外层 builder_；子函数字节码交给调用方。
   * 注意：子函数局部槽数（参数 + var）必须写入字节码，否则 VM 栈底
   * 大小（numLocals）为 0，参数 LoadVar/StoreVar 越界报错或错位。 */
  builder_.set_num_locals(scope_->next_slot);
  FunctionBytecode child_fn = builder_.build();
  builder_ = std::move(saved_builder);
  scope_ = child->parent;
  /* 释放子作用域帧（scopes_ 中最后一个） */
  scopes_.pop_back();
  delete child;

  if (!ok) return false;
  /* M5.1d-b 箭头函数：super 上下文继承标记（MakeFunction 时复制外层
   * HomeObject，使箭头函数内 super 指向外层方法的 HomeObject）。 */
  child_fn.is_arrow = (fn.type == NodeType::ArrowFunctionExpression);
  *out = std::move(child_fn);
  return true;
}

/* 嵌套函数编译（M2.5）：编译子函数字节码加入当前 builder 的 nested 表，
 * 发射 MakeFunction 指令。声明时挂到函数名局部变量；表达式时留栈顶。 */
bool Compiler::compileNestedFunction(const ASTNode &fn, bool is_declaration) {
  FunctionBytecode child;
  if (!compileChildFunction(fn, &child)) return false;

  uint16_t index = static_cast<uint16_t>(builder_.add_nested(std::move(child)));
  builder_.make_function(index);

  if (is_declaration) {
    /* 函数声明：按函数名声明局部槽并存储。 */
    if (fn.fn_has_name && !fn.children.empty()) {
      uint16_t slot = declareLocal(fn.children[0]->name);
      builder_.store_var(slot);
    }
  }
  return true;
}

/* 编译单个类方法/构造器函数（M5.1d-a）：
 *   fn       方法函数表达式（FunctionExpression，fn_has_name=false）
 *   fn_name  函数名（方法键名；构造器用类名）
 *   is_ctor  true = 构造器（is_constructor 标志）
 * 产出子函数字节码加入 nested 表并发射 MakeFunction，函数对象留栈顶。 */
bool Compiler::compileClassMemberFunction(const ASTNode &fn,
                                          const std::string &fn_name,
                                          bool is_ctor) {
  FunctionBytecode child;
  if (!compileChildFunction(fn, &child)) return false;
  child.name = fn_name;
  if (is_ctor) {
    child.is_constructor = true;
  } else {
    child.is_method = true;
  }
  uint16_t index = static_cast<uint16_t>(builder_.add_nested(std::move(child)));
  builder_.make_function(index);
  return true;
}

/* 编译类元素（M5.1d-a）：把方法/访问器挂到目标对象。
 * 目标：static -> 构造器（栈顶，Dup 保留）；实例 -> proto 槽。 */
bool Compiler::compileClassElementDef(const ASTNode &prop, uint16_t proto_slot,
                                      bool is_static) {
  if (prop.children.empty()) {
    fail("invalid class element");
    return false;
  }
  /* 键：计算键 [expr] 运行期求值；否则编译为常量串。 */
  std::string key;
  bool computed_key = prop.prop.is_computed;
  if (!computed_key && !objectKeyToString(prop.children[0], &key)) {
    fail("invalid class element key");
    return false;
  }
  const PropertyKind kind = prop.prop.kind;
  if (prop.children.size() < 2) {
    fail("invalid class element (missing function)");
    return false;
  }
  const ASTNode *fn = prop.children[1];

  if (is_static) {
    builder_.dup(); /* 构造器在栈顶，dup 保留原值 */
  } else {
    builder_.load_var(proto_slot);
  }
  if (computed_key) {
    if (!compileExpr(*prop.children[0])) return false;
  } else {
    builder_.push_const(key);
  }
  if (!compileClassMemberFunction(*fn, key, /*is_ctor=*/false)) return false;
  /* M5.1d-b super：设置方法 [[HomeObject]] = 定义时所在对象
   * （实例方法 = proto，static = 构造器）。栈 [obj, key, fn] 原地。 */
  builder_.set_home_object();
  if (kind == PropertyKind::Method) {
    builder_.define_prop();
  } else {
    builder_.define_accessor(kind == PropertyKind::Set ? 1 : 0);
  }
  builder_.pop(); /* 丢弃定义结果；static 时留下 dup 的构造器 */
  return true;
}

/* class 编译（M5.1d-a）：
 * children 布局：[name?, extends?, element...]（见 parser_stmt.cc parseClassTail）。
 *
 * 产物 = 构造器函数（栈顶；声明时挂类名局部变量）。执行序列：
 *   NewObject; StoreVar proto_tmp          ; prototype 对象
 *   [extends] 求值基类 -> StoreVar base_tmp
 *   MakeFunction ctor（构造器方法体或默认空构造器）
 *   Dup; "prototype"; proto_tmp; DefineProp; Pop  ; ctor.prototype = proto
 *   实例方法：LoadVar proto_tmp; key; fn; DefineProp/Accessor; Pop
 *   static 方法：Dup; key; fn; DefineProp/Accessor; Pop
 *   [extends] proto_tmp.[[Prototype]]=base.prototype; ctor.[[Prototype]]=base
 *   结果 = 构造器（栈顶） */
bool Compiler::compileClass(const ASTNode &node, bool is_declaration) {
  std::string class_name;
  size_t index = 0;
  if (!node.children.empty() && node.children[0]->type == NodeType::Identifier) {
    class_name = node.children[0]->name;
    index = 1;
  }
  const ASTNode *base = nullptr;
  if (index < node.children.size() &&
      node.children[index]->type != NodeType::Property) {
    base = node.children[index];
    ++index;
  }

  /* 类名作用域：class 声明时类名绑定。class 体内引用类名（自引用）留
   * M5.1d 后续阶段（需类名在构造器/方法作用域可见）。 */
  uint16_t class_slot = 0;
  if (is_declaration && !class_name.empty()) {
    class_slot = declareLocal(class_name);
  }

  /* 求值 extends 基类（若有），暂存槽。 */
  uint16_t base_slot = 0;
  if (base != nullptr) {
    if (!compileExpr(*base)) return false;
    base_slot = scope_->next_slot++;
    builder_.store_var(base_slot);
  }

  /* 1. prototype 对象。 */
  builder_.new_object();
  uint16_t proto_slot = scope_->next_slot++;
  builder_.store_var(proto_slot);

  /* 2. 编译构造器函数。
   *    找 key == "constructor" 的 Method 元素作为构造器体；否则默认空构造器。 */
  const ASTNode *ctor_fn = nullptr;
  std::vector<const ASTNode *> elements;
  for (size_t i = index; i < node.children.size(); ++i) {
    const ASTNode *elem = node.children[i];
    if (elem == nullptr || elem->type != NodeType::Property) continue;
    elements.push_back(elem);
    if (elem->prop.kind == PropertyKind::Method && !elem->prop.is_static &&
        elem->children.size() >= 2 &&
        elem->children[0]->type == NodeType::Identifier &&
        elem->children[0]->name == "constructor") {
      ctor_fn = elem->children[1];
    }
  }
  if (ctor_fn != nullptr) {
    if (!compileClassMemberFunction(*ctor_fn, class_name, /*is_ctor=*/true)) {
      return false;
    }
  } else {
    /* 默认构造器：直接返回 this（construct() 以新对象为结果）。
     * 显式构造字节码（无参数）。 */
    FunctionBytecode ctor_bc;
    ctor_bc.name = class_name;
    ctor_bc.is_constructor = true;
    ctor_bc.numLocals = 0;
    ctor_bc.code.push_back({Opcode::LoadThis, 0});
    ctor_bc.code.push_back({Opcode::Ret, 0});
    uint16_t ctor_index =
        static_cast<uint16_t>(builder_.add_nested(std::move(ctor_bc)));
    builder_.make_function(ctor_index);
  }

  /* 3. ctor.prototype = proto。栈上 [ctor]。
   *    M5.1d-b super：构造器 [[HomeObject]] = 构造器自身（static 语义），
   *    super.x 从 ctor.[[Prototype]] = base 读取（extends 后）。 */
  builder_.dup();
  builder_.set_home_object_self();
  /* set_home_object_self（arg=1）VM 端已把 obj 槽覆盖为 fn 并去重：
   * 栈 [ctor] -> [ctor]（self 形式 [obj, fn] -> [fn]），无需再 pop。 */
  builder_.dup();
  builder_.push_const("prototype");
  builder_.load_var(proto_slot);
  builder_.define_prop();
  builder_.pop();

  /* 4. 实例方法挂到 proto；static 方法挂到构造器（栈顶 dup）。
   *    构造器元素本身不重复挂载（constructor 是构造器，不是 prototype 方法）。 */
  for (const ASTNode *elem : elements) {
    const ASTNode *elem_fn =
        (elem->children.size() >= 2) ? elem->children[1] : nullptr;
    if (ctor_fn != nullptr && elem_fn == ctor_fn) continue;
    if (!compileClassElementDef(*elem, proto_slot, elem->prop.is_static)) {
      return false;
    }
  }

  /* 5. extends：proto_tmp.[[Prototype]] = base.prototype；
   *          ctor.[[Prototype]] = base。 */
  if (base != nullptr) {
    builder_.load_var(proto_slot);
    builder_.load_var(base_slot);
    builder_.load_prop("prototype");
    builder_.set_prototype();
    builder_.pop(); /* 丢弃 proto（SetPrototype 压回 obj） */
    builder_.dup();
    builder_.load_var(base_slot);
    builder_.set_prototype();
    builder_.pop(); /* 丢弃 dup 的构造器 */
  }

  /* 6. 结果：构造器留栈顶。声明时挂类名局部变量。 */
  if (is_declaration && !class_name.empty()) {
    builder_.store_var(class_slot);
  }
  return true;
}

std::vector<const ASTNode *> Compiler::collectTopLevelFunctions(const AST &ast) {
  std::vector<const ASTNode *> out;
  if (ast.root == nullptr) return out;
  for (const ASTNode *stmt : ast.root->children) {
    if (stmt != nullptr && stmt->type == NodeType::FunctionDeclaration) {
      out.push_back(stmt);
    }
  }
  return out;
}

CompileResult Compiler::failResult() {
  CompileResult result;
  result.ok = false;
  result.error = error_;
  return result;
}

/* -------------------------------------------------------------------------- */
/* 解构编译（M5.1a）                                                          */
/* -------------------------------------------------------------------------- */

/* 预声明解构模式中的全部绑定名（声明语义：槽位分配稳定）。 */
bool Compiler::declarePatternBindings(const ASTNode &pattern) {
  if (pattern.type == NodeType::ObjectPattern) {
    for (const ASTNode *member : pattern.children) {
      if (member == nullptr) continue;
      if (member->type == NodeType::SpreadElement) {
        if (!member->children.empty() &&
            !declarePatternBindings(*member->children[0]))
          return false;
        continue;
      }
      if (member->type != NodeType::Property || member->children.empty()) {
        return false;
      }
      /* 简写 { p }：单 child（key 即 value），value = key。 */
      const ASTNode *value =
          member->children.size() >= 2 ? member->children[1] : member->children[0];
      if (!declarePatternBindings(*value)) return false;
    }
    return true;
  }
  if (pattern.type == NodeType::ArrayPattern) {
    for (const ASTNode *elem : pattern.children) {
      if (elem == nullptr) continue;
      if (elem->type == NodeType::SpreadElement) {
        if (!elem->children.empty() &&
            !declarePatternBindings(*elem->children[0]))
          return false;
        continue;
      }
      if (!declarePatternBindings(*elem)) return false;
    }
    return true;
  }
  if (pattern.type == NodeType::Identifier) {
    declareLocal(pattern.name);
    return true;
  }
  if (pattern.type == NodeType::AssignmentPattern) {
    /* 默认值：绑定左侧目标。 */
    if (!pattern.children.empty()) return declarePatternBindings(*pattern.children[0]);
    return false;
  }
  if (pattern.type == NodeType::RestElement) {
    if (!pattern.children.empty()) return declarePatternBindings(*pattern.children[0]);
    return false;
  }
  return false;
}

/* 绑定单个目标（值已在栈顶）：
 *   Identifier      -> 声明 declareLocal/store 或赋值 resolveAssignTarget/store
 *   AssignmentPattern -> 默认值：undefined 时求默认表达式
 *   嵌套模式        -> 存临时槽后递归 compilePattern
 * 绑定后值仍留栈顶（调用方 Pop 或复用）。 */
bool Compiler::compileBindTarget(const ASTNode &target, bool is_declaration) {
  if (target.type == NodeType::Identifier) {
    if (is_declaration) {
      uint16_t slot = declareLocal(target.name);
      builder_.store_var(slot);
      builder_.load_var(slot); /* 恢复绑定值到栈顶（外层 Pop 复用） */
    } else {
      uint16_t slot = 0, capture = 0;
      bool is_upvalue = false;
      if (!resolveAssignTarget(target.name, &slot, &capture, &is_upvalue)) {
        fail("not implemented: assignment to undeclared global '" +
             target.name + "'");
        return false;
      }
      if (is_upvalue) {
        builder_.store_upvalue(capture);
        builder_.load_upvalue(capture); /* 保留绑定值在栈顶 */
      } else {
        builder_.store_var(slot);
        builder_.load_var(slot);
      }
    }
    return true;
  }

  if (target.type == NodeType::AssignmentPattern) {
    /* 默认值：value = (value === undefined) ? default : value
     *   [value] JumpIfUndefined use_default; Jump end
     *   use_default: [default]            ; end:
     * 注意：JumpIfUndefined 弹栈。选定后递归绑定左侧目标（值在栈顶）。 */
    if (target.children.size() < 2) {
      fail("invalid default value pattern");
      return false;
    }
    BcLabel use_default = builder_.new_label();
    BcLabel end = builder_.new_label();
    builder_.jump_if_undefined(&use_default);
    builder_.jump(&end);
    builder_.bind_label(use_default);
    if (!compileExpr(*target.children[1])) return false;
    builder_.bind_label(end);
    /* 递归绑定左侧目标（Identifier / 嵌套模式；绑定后值留栈顶）。 */
    return compileBindTarget(*target.children[0], is_declaration);
  }

  if (target.type == NodeType::ObjectPattern ||
      target.type == NodeType::ArrayPattern) {
    /* 嵌套模式：值暂存临时槽后递归，恢复源值到栈顶（外层 Pop 复用）。 */
    uint16_t tmp = scope_->next_slot++;
    builder_.store_var(tmp);
    if (!compilePattern(target, tmp, is_declaration)) return false;
    builder_.load_var(tmp);
    return true;
  }

  fail("invalid destructuring target");
  return false;
}

/* 解构模式编译：源在临时槽 src_slot，逐元素绑定。
 * 对象模式：LoadVar src; LoadProp key（值在栈顶）-> 绑定
 * 数组模式：LoadVar src; PushInt i; LoadElem（值在栈顶）-> 绑定；
 *           rest（SpreadElement 在数组模式末尾）用 RestToArray。 */
bool Compiler::compilePattern(const ASTNode &pattern, uint16_t src_slot,
                              bool is_declaration) {
  if (pattern.type == NodeType::ObjectPattern) {
    for (const ASTNode *member : pattern.children) {
      if (member == nullptr) continue;
      if (member->type == NodeType::SpreadElement) {
        /* 对象 rest：{a, ...rest}（ES2018）。rest = 源剩余属性。
         * 用 RestToArray 不适用（对象非迭代器）；本阶段暂不支持，
         * M5.1a 先支持数组 rest，对象 rest 留 M5.1e。 */
        fail("not implemented: object rest in destructuring");
        return false;
      }
      if (member->type != NodeType::Property || member->children.empty()) {
        fail("invalid object pattern member");
        return false;
      }
      /* 简写 { p }：单 child（key 即 value），value = key。 */
      const ASTNode *value =
          member->children.size() >= 2 ? member->children[1] : member->children[0];
      /* key */
      std::string key;
      if (!objectKeyToString(member->children[0], &key)) {
        fail("not implemented: computed key in destructuring");
        return false;
      }
      builder_.load_var(src_slot);
      builder_.load_prop(key); /* 值在栈顶 */
      if (!compileBindTarget(*value, is_declaration)) {
        return false;
      }
      builder_.pop(); /* 丢弃绑定值 */
    }
    return true;
  }

  if (pattern.type == NodeType::ArrayPattern) {
    uint16_t index = 0;
    for (const ASTNode *elem : pattern.children) {
      if (elem == nullptr) {
        /* 空洞：跳过（无绑定），但索引仍递增。 */
        ++index;
        continue;
      }
      if (elem->type == NodeType::SpreadElement) {
        /* rest：...target（必须为数组模式最后一个元素，Parser 已保证）。
         * LoadVar src; PushInt index; RestToArray -> 剩余数组 */
        if (elem->children.empty()) {
          fail("invalid rest element");
          return false;
        }
        builder_.load_var(src_slot);
        builder_.push_int(index);
        builder_.rest_to_array();
        if (!compileBindTarget(*elem->children[0], is_declaration)) {
          return false;
        }
        builder_.pop();
        return true; /* rest 之后无更多元素 */
      }
      builder_.load_var(src_slot);
      builder_.push_int(index);
      builder_.load_elem(); /* 值在栈顶 */
      if (!compileBindTarget(*elem, is_declaration)) {
        return false;
      }
      builder_.pop(); /* 丢弃绑定值 */
      ++index;
    }
    return true;
  }

  fail("invalid pattern");
  return false;
}

/* 展开参数列表（M5.1a）：把 args 元素追加到槽 slot 的数组。
 * 非展开元素求值后 AppendElem（动态索引追加）；展开元素求值后
 * AppendSpread（迭代追加）。求值顺序 = 源码顺序（§13.2.3 等）。 */
bool Compiler::compileSpreadArgs(const std::vector<const ASTNode *> &args,
                                 uint16_t slot) {
  for (const ASTNode *arg : args) {
    if (arg == nullptr) continue;
    if (arg->type == NodeType::SpreadElement) {
      if (arg->children.empty() || !compileExpr(*arg->children[0])) return false;
      builder_.append_spread(slot);
    } else {
      if (!compileExpr(*arg)) return false;
      builder_.append_elem(slot);
    }
  }
  return true;
}

/* 变量声明：每个声明器分配局部槽 + 编译初始化表达式（声明值 = undefined）。 */
bool Compiler::compileVarDecl(const ASTNode &decl) {
  for (const ASTNode *declarator : decl.children) {
    if (declarator == nullptr || declarator->type != NodeType::VariableDeclarator) {
      fail("invalid variable declarator");
      return false;
    }
    const ASTNode *target = declarator->children[0];
    if (target->type == NodeType::ObjectPattern ||
        target->type == NodeType::ArrayPattern) {
      /* 解构声明（M5.1a）：先声明所有绑定名，再编译 init 并解构。
       * 无 init 的解构声明非法（Parser 已对 const 校验；var/let 缺省
       * 语义为解构 undefined，本阶段直接报错）。 */
      if (declarator->children.size() < 2) {
        fail("destructuring declaration requires initializer");
        return false;
      }
      /* 预声明绑定名（保证槽位分配稳定）。 */
      if (!declarePatternBindings(*target)) return false;
      if (!compileExpr(*declarator->children[1])) return false;
      uint16_t tmp = scope_->next_slot++;
      builder_.store_var(tmp);
      if (!compilePattern(*target, tmp, /*is_declaration=*/true)) return false;
      continue;
    }
    if (target->type != NodeType::Identifier) {
      fail("invalid variable declarator target");
      return false;
    }
    uint16_t slot = declareLocal(target->name);

    /* 初始化：= expr（未初始化时槽位为 undefined，由 runBytecode 填充）。 */
    if (declarator->children.size() >= 2) {
      if (!compileExpr(*declarator->children[1])) return false;
      builder_.store_var(slot);
    }
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* 语句编译（M2.4 ②）                                                        */
/* -------------------------------------------------------------------------- */

bool Compiler::compileStmt(const ASTNode &node) {
  switch (node.type) {
    case NodeType::ExprStmt:
      if (!node.children.empty() && !compileExpr(*node.children[0])) return false;
      builder_.pop(); /* 表达式语句值丢弃 */
      return true;
    case NodeType::VariableDeclaration:
      return compileVarDecl(node);
    case NodeType::EmptyStatement:
      return true;
    case NodeType::BlockStatement:
      return compileBlock(node);
    case NodeType::IfStatement:
      return compileIf(node);
    case NodeType::WhileStatement:
      return compileWhile(node);
    case NodeType::DoWhileStatement:
      return compileDoWhile(node);
    case NodeType::ForStatement:
      return compileFor(node);
    case NodeType::ForOfStatement:
      return compileForOf(node);
    case NodeType::ForInStatement:
      fail("not implemented: for-in loop");
      return false;
    case NodeType::ReturnStatement:
      return compileReturn(node);
    case NodeType::FunctionDeclaration:
      /* 嵌套函数声明（M2.5 闭包）：编译子函数字节码，发射 MakeFunction
       * 实例化闭包并挂到当前函数局部变量。 */
      return compileNestedFunction(node);
    case NodeType::ClassDeclaration:
      /* class 声明（M5.1d-a）：编译构造器函数并挂到类名局部变量。 */
      return compileClass(node, /*is_declaration=*/true);
    case NodeType::BreakStatement:
    case NodeType::ContinueStatement:
      fail("not implemented: break/continue (M2.4 ③ 循环控制)");
      return false;
    default:
      fail("not implemented: statement type " +
           std::to_string(static_cast<int>(node.type)));
      return false;
  }
}

bool Compiler::compileBlock(const ASTNode &node) {
  /* 块语句：逐条编译（值均丢弃）。本阶段不引入新作用域。 */
  for (const ASTNode *stmt : node.children) {
    if (stmt == nullptr) continue;
    if (!compileStmt(*stmt)) return false;
  }
  return true;
}

bool Compiler::compileIf(const ASTNode &node) {
  /* if (cond) then [else]
   *   [cond]
   *   JumpIfFalse else_label
   *   [then]
   *   Jump end_label
   * else_label:
   *   [else]        （无 else 时为空）
   * end_label: */
  if (node.children.empty()) {
    fail("if without condition");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  BcLabel else_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.jump_if_false(&else_label);
  if (node.children.size() >= 2 && !compileStmt(*node.children[1])) return false;
  builder_.jump(&end_label);
  builder_.bind_label(else_label);
  if (node.children.size() >= 3 && !compileStmt(*node.children[2])) return false;
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileWhile(const ASTNode &node) {
  /* while (cond) body
   * loop_label:
   *   [cond]
   *   JumpIfFalse end_label
   *   [body]
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 2) {
    fail("while without condition/body");
    return false;
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  if (!compileExpr(*node.children[0])) return false;
  builder_.jump_if_false(&end_label);
  if (!compileStmt(*node.children[1])) return false;
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileDoWhile(const ASTNode &node) {
  /* do body while (cond)
   * loop_label:
   *   [body]
   *   [cond]
   *   JumpIfTrue loop_label
   * end_label:
   * 注意：VM 无 JumpIfTrue，用 Not + JumpIfFalse：
   *   cond falsy -> 结束；cond truthy -> 循环。
   *   [cond] Not JumpIfFalse loop_label  —— cond falsy(Not=true) 不跳(错误！)
   * 需重新推导：跳回 loop 当 cond truthy。
   *   等价：cond falsy 时结束。用 JumpIfFalse 直接跳 end：
   *   [cond] JumpIfFalse end_label；Jump loop_label。
   *   loop_label:
   *   [body]
   *   [cond]
   *   JumpIfFalse end_label
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 2) {
    fail("do-while without body/condition");
    return false;
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  if (!compileStmt(*node.children[0])) return false;
  if (!compileExpr(*node.children[1])) return false;
  builder_.jump_if_false(&end_label);
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileFor(const ASTNode &node) {
  /* for (init; cond; update) body
   *   [init]        （可空）
   * loop_label:
   *   [cond]        （可空；空 = true）
   *   JumpIfFalse end_label
   *   [body]
   * update_label:
   *   [update]      （可空）
   *   Jump loop_label
   * end_label: */
  if (node.children.size() < 4) {
    fail("for without clauses");
    return false;
  }
  /* init */
  if (node.children[0] != nullptr) {
    if (node.children[0]->type == NodeType::VariableDeclaration) {
      if (!compileVarDecl(*node.children[0])) return false;
    } else {
      /* init 表达式：求值后丢弃。 */
      if (!compileExpr(*node.children[0])) return false;
      builder_.pop();
    }
  }
  BcLabel loop_label = builder_.new_label();
  BcLabel update_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  /* cond */
  if (node.children[1] != nullptr) {
    if (!compileExpr(*node.children[1])) return false;
    builder_.jump_if_false(&end_label);
  }
  /* body */
  if (node.children[3] != nullptr && !compileStmt(*node.children[3])) return false;
  /* update */
  builder_.bind_label(update_label);
  if (node.children[2] != nullptr) {
    if (!compileExpr(*node.children[2])) return false;
    builder_.pop(); /* update 值丢弃 */
  }
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileForOf(const ASTNode &node) {
  /* for (left of right) body  （M4.4f，§14.7.5）
   *   [right]              ; 可迭代对象
   *   GetIterator          ; -> iterator
   *   StoreVar iter_tmp    ; 迭代器存临时槽
   * loop_label:
   *   LoadVar iter_tmp
   *   IteratorStep         ; -> [value, done]（done 在栈顶）
   *   JumpIfTrue end_label ; done true 弹栈跳 end；done false 继续（value 留栈顶）
   *   [left 赋值]          ; 栈顶 value -> 变量/成员
   *   [body]
   *   Jump loop_label
   * end_label:
   *   Pop                  ; 清完成路径残留 value（undefined）
   *
   * children = [left, right, body]；left 可为 VariableDeclaration 或表达式。 */
  if (node.children.size() < 3) {
    fail("for-of without clauses");
    return false;
  }
  const ASTNode *left = node.children[0];
  const ASTNode *right = node.children[1];
  const ASTNode *body = node.children[2];

  /* 可迭代对象求值。 */
  if (!compileExpr(*right)) return false;
  builder_.get_iterator();

  /* 迭代器存临时槽（跨循环迭代保活，栈根集保护）。
   * 用内部名 declareLocal：占住 locals 表，避免与循环体内
   * declareLocal(x) 复用同一槽号（无名 next_slot++ 会被覆盖）。
   * 名字带唯一计数：嵌套 for-of 的内层不复用外层迭代器槽。 */
  uint16_t iter_tmp =
      declareLocal("\x01iter" + std::to_string(iter_slot_seq_++));
  builder_.store_var(iter_tmp);

  BcLabel loop_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.bind_label(loop_label);
  builder_.load_var(iter_tmp);
  builder_.iterator_step();
  builder_.jump_if_true(&end_label); /* done true -> 结束循环 */

  /* left 赋值：栈顶是当前迭代 value。 */
  if (left->type == NodeType::VariableDeclaration) {
    /* for (var x of ...)：声明 x 局部槽，栈顶 value 直接 StoreVar。 */
    for (const ASTNode *declarator : left->children) {
      if (declarator == nullptr ||
          declarator->type != NodeType::VariableDeclarator ||
          declarator->children.empty() ||
          declarator->children[0]->type != NodeType::Identifier) {
        fail("not implemented: destructuring in for-of");
        return false;
      }
      uint16_t slot = declareLocal(declarator->children[0]->name);
      builder_.store_var(slot);
    }
  } else if (left->type == NodeType::Identifier) {
    /* for (x of ...)：赋值到既有变量。 */
    uint16_t slot = 0;
    uint16_t capture = 0;
    bool is_upvalue = false;
    if (!resolveAssignTarget(left->name, &slot, &capture, &is_upvalue)) {
      fail("not implemented: assignment to undeclared global in for-of");
      return false;
    }
    if (is_upvalue) {
      builder_.store_upvalue(capture);
    } else {
      builder_.store_var(slot);
    }
  } else {
    fail("not implemented: for-of left target (member/destructuring)");
    return false;
  }

  if (body != nullptr && !compileStmt(*body)) return false;
  builder_.jump(&loop_label);
  builder_.bind_label(end_label);
  builder_.pop(); /* 清完成路径残留 value */
  return true;
}

bool Compiler::compileReturn(const ASTNode &node) {
  /* return [expr]
   *   [expr]        （可空 -> undefined）
   *   Ret */
  if (node.children.empty()) {
    builder_.push_op(Opcode::PushUndefined);
  } else if (!compileExpr(*node.children[0])) {
    return false;
  }
  builder_.ret();
  return true;
}

/* -------------------------------------------------------------------------- */
/* 表达式编译                                                                  */
/* -------------------------------------------------------------------------- */

bool Compiler::compileExpr(const ASTNode &node) {
  switch (node.type) {
    case NodeType::Literal: return compileLiteral(node);
    case NodeType::ObjectLiteral: return compileObjectLiteral(node);
    case NodeType::ArrayLiteral: return compileArrayLiteral(node);
    case NodeType::Identifier: return compileIdentifier(node);
    case NodeType::ThisExpression: {
      /* M5.1c：this -> LoadThis（当前帧 this）。 */
      builder_.load_this();
      return true;
    }
    case NodeType::UnaryOp: return compileUnary(node);
    case NodeType::BinaryOp: return compileBinary(node);
    case NodeType::Conditional: return compileConditional(node);
    case NodeType::Assignment: return compileAssignment(node);
    case NodeType::CallExpression: return compileCall(node);
    case NodeType::Sequence: return compileSequence(node);
    case NodeType::MemberExpression: return compileMemberLoad(node);
    case NodeType::SuperProperty: {
      /* M5.1d-b super 属性访问：super.x / super[expr]。
       * 从当前方法 HomeObject 的 [[Prototype]] 读取（VM 经 self）。 */
      if (node.children.empty()) {
        fail("super property without key");
        return false;
      }
      if (node.member.optional) {
        fail("not implemented: optional chaining on super");
        return false;
      }
      if (!node.member.computed) {
        const ASTNode *prop = node.children[0];
        if (prop->type != NodeType::Identifier) {
          fail("invalid super property name");
          return false;
        }
        builder_.super_load_prop(prop->name);
        return true;
      }
      if (!compileExpr(*node.children[0])) return false;
      builder_.super_load_elem();
      return true;
    }
    case NodeType::NewExpression: return compileNew(node);
    case NodeType::FunctionExpression:
    case NodeType::ArrowFunctionExpression:
      /* 函数表达式/箭头函数（M2.5 闭包）：编译子函数，MakeFunction 留栈顶。 */
      return compileNestedFunction(node, /*is_declaration=*/false);
    case NodeType::ClassExpression:
      /* class 表达式（M5.1d-a）：编译构造器函数，留栈顶。 */
      return compileClass(node, /*is_declaration=*/false);
    case NodeType::UpdateExpression:
      fail("not implemented: update expression");
      return false;
    default:
      fail("not implemented: expression type " +
           std::to_string(static_cast<int>(node.type)));
      return false;
  }
}

bool Compiler::compileLiteral(const ASTNode &node) {
  const LiteralPayload &lit = node.literal;
  if (lit.is_bigint) {
    /* BigInt 字面量（M4.3）：PushBigInt 常量池条目（十进制数字串）。
     * 注意必须在 is_number 之前判断——parser 对 BigInt 同时置位
     * is_number（double 载荷）与 is_bigint（精确数字串）。
     * 字面量本身恒非负（负号由一元 Neg 处理，如 -1n）。 */
    builder_.push_bigint(lit.bigint_str, false);
    return true;
  }
  if (lit.is_number) {
    builder_.push_number(lit.number);
    return true;
  }
  if (lit.is_string) {
    builder_.push_const(lit.str);
    return true;
  }
  if (lit.is_boolean) {
    builder_.push_op(lit.bool_value ? Opcode::PushTrue : Opcode::PushFalse);
    return true;
  }
  if (lit.is_null) {
    builder_.push_op(Opcode::PushNull);
    return true;
  }
  builder_.push_op(Opcode::PushUndefined);
  return true;
}

bool Compiler::compileIdentifier(const ASTNode &node) {
  return emitLoadIdentifier(node.name);
}

/* 对象字面量键 -> 字符串（M3.1）：
 *   Identifier 键 {a: 1}      -> 名字
 *   String 字面量键 {"k": 1}  -> 解码串
 *   Number 字面量键 {1: 1}    -> 数值最短表示（§6.1.6.1.20）
 * 计算键 [expr] / Symbol 键 / getter/setter/method 留后续阶段。 */
bool Compiler::objectKeyToString(const ASTNode *key, std::string *out) {
  if (key->type == NodeType::Identifier) {
    *out = key->name;
    return true;
  }
  if (key->type == NodeType::Literal) {
    const LiteralPayload &lit = key->literal;
    if (lit.is_string) {
      *out = lit.str;
      return true;
    }
    if (lit.is_number) {
      *out = numberToString(lit.number);
      return true;
    }
  }
  return false; /* 计算键/其他：本阶段不支持 */
}

/* 对象字面量（M3.1）：{ key: value, shorthand, ...spread }
 *   NewObject; StoreVar tmp
 *   对每属性：LoadVar tmp; [value]; StoreProp "key"; Pop
 *   最后 LoadVar tmp（结果 = 对象）
 * 简写 {b}：值 = 读 b（emitLoadIdentifier）。 */
bool Compiler::compileObjectLiteral(const ASTNode &node) {
  builder_.new_object();
  uint16_t tmp = scope_->next_slot++;
  builder_.store_var(tmp);

  for (const ASTNode *prop : node.children) {
    if (prop == nullptr) continue;
    if (prop->type == NodeType::SpreadElement) {
      /* 对象展开（M5.1a，§12.2.6.9 CopyDataProperties）：
       * 求值源对象，own enumerable 字符串属性复制进 tmp。 */
      if (prop->children.empty() || !compileExpr(*prop->children[0])) {
        return false;
      }
      builder_.copy_object_props(tmp);
      continue;
    }
    if (prop->type != NodeType::Property) {
      fail("invalid object literal member");
      return false;
    }
    if (prop->children.empty()) {
      fail("invalid property");
      return false;
    }
    /* 键：计算键 [expr] 运行期求值；否则编译为常量串。 */
    std::string key;
    bool computed_key = prop->prop.is_computed;
    if (!computed_key && !objectKeyToString(prop->children[0], &key)) {
      fail("invalid property key");
      return false;
    }
    const PropertyKind kind = prop->prop.kind;
    builder_.load_var(tmp); /* obj */
    if (kind == PropertyKind::Method || kind == PropertyKind::Get ||
        kind == PropertyKind::Set) {
      /* M5.1c method/getter/setter：children[1] 为方法函数表达式。 */
      if (prop->children.size() < 2) {
        fail("invalid method property");
        return false;
      }
      if (computed_key) {
        if (!compileExpr(*prop->children[0])) return false;
      } else {
        builder_.push_const(key);
      }
      if (!compileExpr(*prop->children[1])) return false;
      if (kind == PropertyKind::Method) {
        /* method 是数据属性（函数值），用 DefineProp 定义。 */
        builder_.define_prop();
      } else {
        builder_.define_accessor(kind == PropertyKind::Set ? 1 : 0);
      }
      builder_.pop(); /* 丢弃定义结果 */
      continue;
    }
    if (computed_key) {
      /* 计算键 { [expr]: value }：obj, key, value -> DefineProp。 */
      if (!compileExpr(*prop->children[0])) return false;
      if (prop->children.size() < 2 || !compileExpr(*prop->children[1])) {
        return false;
      }
      builder_.define_prop();
      builder_.pop();
      continue;
    }
    if (kind == PropertyKind::Shorthand) {
      /* {b}：值 = 读 b（简写属性值取自同名绑定）。 */
      if (prop->children[0]->type != NodeType::Identifier) {
        fail("invalid shorthand property");
        return false;
      }
      if (!emitLoadIdentifier(prop->children[0]->name)) return false;
    } else {
      if (prop->children.size() < 2 || !compileExpr(*prop->children[1])) {
        return false;
      }
    }
    builder_.store_prop(key);
    builder_.pop(); /* 丢弃赋值结果 */
  }
  builder_.load_var(tmp); /* 结果 = 对象 */
  return true;
}

/* 数组字面量（M3.1）：[ elem, ...spread ]
 *   NewArray; StoreVar tmp
 *   对每元素（索引 i）：LoadVar tmp; PushInt i; [elem]; StoreElem; Pop
 *   最后 LoadVar tmp（结果 = 数组）
 * 稀疏空洞（[1,,3]）本阶段按紧凑存储（元素个数 = 声明元素数）。 */
bool Compiler::compileArrayLiteral(const ASTNode &node) {
  builder_.new_array();
  uint16_t tmp = scope_->next_slot++;
  builder_.store_var(tmp);

  uint16_t index = 0;
  bool has_spread = false;
  for (const ASTNode *elem : node.children) {
    if (elem != nullptr && elem->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }
  if (has_spread) {
    /* 含展开：全部元素动态追加（展开前后顺序一致，索引动态）。
     * 空洞（nullptr）无值，不追加（保持紧凑存储语义）。 */
    for (const ASTNode *elem : node.children) {
      if (elem == nullptr) continue;
      if (elem->type == NodeType::SpreadElement) {
        if (elem->children.empty() || !compileExpr(*elem->children[0])) {
          return false;
        }
        builder_.append_spread(tmp);
      } else {
        if (!compileExpr(*elem)) return false;
        builder_.append_elem(tmp);
      }
    }
    builder_.load_var(tmp); /* 结果 = 数组 */
    return true;
  }
  for (const ASTNode *elem : node.children) {
    if (elem == nullptr) continue;
    builder_.load_var(tmp);   /* arr */
    builder_.push_int(index); /* arr, i */
    if (!compileExpr(*elem)) return false;
    builder_.store_elem();
    builder_.pop(); /* 丢弃赋值结果 */
    ++index;
  }
  builder_.load_var(tmp); /* 结果 = 数组 */
  return true;
}

/* 成员访问读取（M3.1）：obj.prop -> LoadProp；obj[expr] -> LoadElem。
 * 先编译对象表达式（留栈顶），再编译属性名/计算键，发射读取指令。
 * 可选链 ?. / ?.[ 本阶段不支持（M5 语义）。 */
bool Compiler::compileMemberLoad(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("member expression without property");
    return false;
  }
  if (node.member.optional) {
    fail("not implemented: optional chaining");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  if (!node.member.computed) {
    /* obj.prop：属性名为 Identifier 或 PrivateIdentifier。 */
    const ASTNode *prop = node.children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member access");
      return false;
    }
    if (prop->type != NodeType::Identifier) {
      fail("invalid member property name");
      return false;
    }
    builder_.load_prop(prop->name);
    return true;
  }
  /* obj[expr]：编译计算键表达式，LoadElem。 */
  if (!compileExpr(*node.children[1])) return false;
  builder_.load_elem();
  return true;
}

/* 成员赋值（M3.2）：obj.prop = rhs / obj[expr] = rhs（含复合 op=）。
 * lhs = node.children[0]（对象表达式），rhs = node.children[1]。
 *
 * 简单赋值：obj.prop = rhs
 *   [obj] [rhs] StoreProp "prop"   -> 栈顶 val
 *   obj[expr] = rhs：
 *   [obj] [expr] [rhs] StoreElem   -> 栈顶 val
 *
 * 复合赋值：obj.prop += rhs（对象表达式只求值一次，暂存临时槽）
 *   [obj] StoreVar t_obj
 *   LoadVar t_obj LoadProp "prop"  -> 旧值
 *   [rhs] Add                       -> 新值
 *   LoadVar t_obj [新值] StoreProp "prop" -> 栈顶 val
 *   obj[expr] += rhs：额外暂存 key：
 *   [obj] StoreVar t_obj [expr] StoreVar t_key
 *   LoadVar t_obj LoadVar t_key LoadElem -> 旧值
 *   [rhs] Add
 *   LoadVar t_obj LoadVar t_key [新值] StoreElem
 *
 * 逻辑复合赋值（&&= / ||= / ??=）需短路 + 重复读引用，本阶段不支持。 */
bool Compiler::compileMemberAssign(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("member assignment without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];
  if (lhs->type != NodeType::MemberExpression) {
    fail("invalid member assignment target");
    return false;
  }
  if (lhs->member.optional) {
    fail("not implemented: optional chaining assignment");
    return false;
  }
  const bool computed = lhs->member.computed;
  const ASTNode *obj_expr = lhs->children[0];
  const ASTNode *key_expr = computed ? lhs->children[1] : nullptr;

  /* 非计算成员：属性名（Identifier；PrivateIdentifier 不支持）。 */
  std::string prop_name;
  if (!computed) {
    const ASTNode *prop = lhs->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member assignment");
      return false;
    }
    if (prop->type != NodeType::Identifier) {
      fail("invalid member property name");
      return false;
    }
    prop_name = prop->name;
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：短路语义（M5.1b）。
   * 对象表达式求值一次（暂存），短路返回旧值（重新读 obj.p），
   * 非短路求 rhs 写回。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
    if (!compileExpr(*obj_expr)) return false;
    uint16_t t_obj = scope_->next_slot++;
    builder_.store_var(t_obj);
    uint16_t t_key = 0;
    if (computed) {
      if (!compileExpr(*key_expr)) return false;
      t_key = scope_->next_slot++;
      builder_.store_var(t_key);
    }
    /* 读旧值。 */
    builder_.load_var(t_obj);
    if (computed) {
      builder_.load_var(t_key);
      builder_.load_elem();
    } else {
      builder_.load_prop(prop_name);
    }
    /* 赋值条件判断：成立跳转赋值路径，否则落短路路径。 */
    BcLabel assign_path = builder_.new_label();
    BcLabel done = builder_.new_label();
    switch (node.assign_op) {
      case AssignmentOpKind::LogicalAndAssign:
        builder_.jump_if_true(&assign_path);
        break;
      case AssignmentOpKind::LogicalOrAssign:
        builder_.jump_if_false(&assign_path);
        break;
      case AssignmentOpKind::NullishAssign:
        builder_.jump_if_nullish(&assign_path);
        break;
      default:
        break;
    }
    /* 短路：返回旧值（重新读 obj.p）。 */
    builder_.load_var(t_obj);
    if (computed) {
      builder_.load_var(t_key);
      builder_.load_elem();
    } else {
      builder_.load_prop(prop_name);
    }
    builder_.jump(&done);
    /* 赋值路径：求 rhs，暂存，压 obj(+key)+rhs 写回（StoreProp 留 rhs 栈顶）。 */
    builder_.bind_label(assign_path);
    if (!compileExpr(*rhs)) return false;
    uint16_t t_val = scope_->next_slot++;
    builder_.store_var(t_val);
    builder_.load_var(t_obj);
    if (computed) builder_.load_var(t_key);
    builder_.load_var(t_val);
    if (computed) {
      builder_.store_elem();
    } else {
      builder_.store_prop(prop_name);
    }
    builder_.bind_label(done);
    return true;
  }

  const bool compound = (node.assign_op != AssignmentOpKind::Assign);
  if (!compound) {
    /* 简单赋值：对象表达式求值一次，留栈顶供 StoreProp/StoreElem 消费。 */
    if (!compileExpr(*obj_expr)) return false;
    if (computed && !compileExpr(*key_expr)) return false;
    if (!compileExpr(*rhs)) return false;
    if (computed) {
      builder_.store_elem();
    } else {
      builder_.store_prop(prop_name);
    }
    return true;
  }

  /* 复合赋值：映射到二元运算。 */
  BinaryOpKind bop = BinaryOpKind::Add;
  if (!compoundAssignToBinary(node.assign_op, &bop)) {
    fail("not implemented: compound assignment operator");
    return false;
  }
  Opcode op = Opcode::Nop;
  if (!binaryOpcode(bop, &op)) {
    fail("not implemented: compound assignment operator");
    return false;
  }

  /* 暂存对象（和计算键）到临时槽，保证对象表达式只求值一次。 */
  if (!compileExpr(*obj_expr)) return false;
  uint16_t t_obj = scope_->next_slot++;
  builder_.store_var(t_obj);
  uint16_t t_key = 0;
  if (computed) {
    if (!compileExpr(*key_expr)) return false;
    t_key = scope_->next_slot++;
    builder_.store_var(t_key);
  }

  /* 读旧值。 */
  builder_.load_var(t_obj);
  if (computed) {
    builder_.load_var(t_key);
    builder_.load_elem();
  } else {
    builder_.load_prop(prop_name);
  }
  /* 运算：新值 = 旧值 op rhs。 */
  if (!compileExpr(*rhs)) return false;
  builder_.push_op(op);

  /* 写回：StoreProp/StoreElem 期望 obj 在下、新值在栈顶。
   * 新值已在栈顶，先暂存到临时槽，再压 obj（和 key）+ 新值。 */
  uint16_t t_val = scope_->next_slot++;
  builder_.store_var(t_val);
  builder_.load_var(t_obj);
  if (computed) builder_.load_var(t_key);
  builder_.load_var(t_val);
  if (computed) {
    builder_.store_elem();
  } else {
    builder_.store_prop(prop_name);
  }
  return true;
}

bool Compiler::compileUnary(const ASTNode &node) {
  if (node.children.empty()) {
    fail("unary op without operand");
    return false;
  }
  if (!compileExpr(*node.children[0])) return false;
  switch (node.unop) {
    case UnaryOpKind::Neg: builder_.push_op(Opcode::Neg); return true;
    case UnaryOpKind::Not: builder_.push_op(Opcode::Not); return true;
    case UnaryOpKind::Typeof: builder_.push_op(Opcode::Typeof); return true;
    case UnaryOpKind::Void:
      /* void x：求值 x（副作用），结果 undefined。 */
      builder_.pop();
      builder_.push_op(Opcode::PushUndefined);
      return true;
    case UnaryOpKind::Pos:
      fail("not implemented: unary + (need ToNumber instruction)");
      return false;
    case UnaryOpKind::BitNot:
      fail("not implemented: unary ~ (need bitwise-not instruction)");
      return false;
    case UnaryOpKind::Delete:
    case UnaryOpKind::Await:
      fail("not implemented: unary delete/await");
      return false;
  }
  return true;
}

bool Compiler::compileBinary(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("binary op without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];

  /* 逻辑短路（&&/||）：短路路径保留 lhs，继续路径为 lhs op rhs。
   * VM 的 JumpIfFalse 会弹栈顶，故先把 lhs 暂存到临时局部槽：
   *   [lhs] store tmp
   *   &&:  load tmp; JumpIfFalse short_circuit   (lhs falsy -> 结果 = lhs)
   *       [rhs]
   *       Jump end
   *       short_circuit: load tmp   (结果 = lhs)
   *       end:
   *   ||:  load tmp; Not; JumpIfFalse short_circuit  (lhs truthy -> 结果 = lhs)
   *       [rhs]
   *       Jump end
   *       short_circuit: load tmp
   *       end: */
  if (node.binop == BinaryOpKind::LogicalAnd ||
      node.binop == BinaryOpKind::LogicalOr) {
    if (!compileExpr(*lhs)) return false;
    uint16_t tmp = scope_->next_slot++; /* 临时槽（不入 locals） */
    builder_.store_var(tmp);
    BcLabel short_circuit = builder_.new_label();
    BcLabel end = builder_.new_label();
    builder_.load_var(tmp);
    if (node.binop == BinaryOpKind::LogicalAnd) {
      /* &&：lhs falsy -> 结果 = lhs（短路）。 */
      builder_.jump_if_false(&short_circuit);
    } else {
      /* ||：lhs truthy -> 结果 = lhs（短路）。VM 无 JumpIfTrue，
       * 用 Not + JumpIfFalse 组合：lhs truthy -> Not -> false -> 跳。 */
      builder_.push_op(Opcode::Not);
      builder_.jump_if_false(&short_circuit);
    }
    if (!compileExpr(*rhs)) return false;
    builder_.jump(&end);
    builder_.bind_label(short_circuit);
    builder_.load_var(tmp); /* 短路路径：结果 = lhs */
    builder_.bind_label(end);
    return true;
  }

  if (node.binop == BinaryOpKind::In || node.binop == BinaryOpKind::Instanceof ||
      node.binop == BinaryOpKind::Nullish) {
    fail("not implemented: in/instanceof/?? operator");
    return false;
  }

  Opcode op = Opcode::Nop;
  if (!binaryOpcode(node.binop, &op)) {
    fail("not implemented: binary operator");
    return false;
  }
  if (!compileExpr(*lhs)) return false;
  if (!compileExpr(*rhs)) return false;
  builder_.push_op(op);
  return true;
}

bool Compiler::compileConditional(const ASTNode &node) {
  if (node.children.size() < 3) {
    fail("conditional without branches");
    return false;
  }
  /* cond ? then : else
   *   [cond]
   *   JumpIfFalse else_label
   *   [then]
   *   Jump end_label
   * else_label:
   *   [else]
   * end_label: */
  if (!compileExpr(*node.children[0])) return false;
  BcLabel else_label = builder_.new_label();
  BcLabel end_label = builder_.new_label();
  builder_.jump_if_false(&else_label);
  if (!compileExpr(*node.children[1])) return false;
  builder_.jump(&end_label);
  builder_.bind_label(else_label);
  if (!compileExpr(*node.children[2])) return false;
  builder_.bind_label(end_label);
  return true;
}

bool Compiler::compileAssignment(const ASTNode &node) {
  if (node.children.size() < 2) {
    fail("assignment without operands");
    return false;
  }
  const ASTNode *lhs = node.children[0];
  const ASTNode *rhs = node.children[1];

  /* 赋值目标：局部标识符 / 捕获的外层变量 / 成员（obj.prop / obj[expr]）。 */
  if (lhs->type == NodeType::MemberExpression) {
    /* 逻辑复合赋值（&&= / ||= / ??=）在成员分支统一处理（M3.2）。 */
    return compileMemberAssign(node);
  }
  if (lhs->type == NodeType::ObjectPattern ||
      lhs->type == NodeType::ArrayPattern) {
    /* 解构赋值（M5.1a）：[a] = arr / {a} = obj。
     * Parser 已保证是纯 = 赋值（复合赋值给解构目标非法）。 */
    if (!compileExpr(*rhs)) return false;
    uint16_t tmp = scope_->next_slot++;
    builder_.store_var(tmp);
    if (!compilePattern(*lhs, tmp, /*is_declaration=*/false)) return false;
    builder_.load_var(tmp); /* 解构赋值表达式值 = 源值（§13.15.2） */
    return true;
  }
  if (lhs->type != NodeType::Identifier) {
    fail("invalid assignment target");
    return false;
  }
  uint16_t slot = 0;
  uint16_t capture = 0;
  bool is_upvalue = false;
  if (!resolveAssignTarget(lhs->name, &slot, &capture, &is_upvalue)) {
    fail("not implemented: assignment to undeclared global '" + lhs->name + "'");
    return false;
  }

  /* 逻辑复合赋值（&&= / ||= / ??=）：短路语义（M5.1b）。
   *   a &&= b：old truthy 求 b 写回；falsy 短路返回 old
   *   a ||= b：old falsy 求 b 写回；truthy 短路返回 old
   *   a ??= b：old nullish 求 b 写回；非 nullish 短路返回 old
   * 发射（统一布局，赋值条件跳转到赋值路径，否则落短路路径）：
   *   LoadVar slot; JumpIfX assign_path; [old] Jump done;
   *   assign_path: [rhs]; StoreVar slot; LoadVar slot; done:
   * 短路路径重新读 slot（JumpIfX 弹栈，且短路时未写回）。 */
  if (node.assign_op == AssignmentOpKind::LogicalAndAssign ||
      node.assign_op == AssignmentOpKind::LogicalOrAssign ||
      node.assign_op == AssignmentOpKind::NullishAssign) {
    BcLabel assign_path = builder_.new_label();
    BcLabel done = builder_.new_label();
    /* 读旧值并按赋值条件跳转。 */
    if (is_upvalue) {
      builder_.load_upvalue(capture);
    } else {
      builder_.load_var(slot);
    }
    switch (node.assign_op) {
      case AssignmentOpKind::LogicalAndAssign:
        builder_.jump_if_true(&assign_path); /* truthy 赋值 */
        break;
      case AssignmentOpKind::LogicalOrAssign:
        builder_.jump_if_false(&assign_path); /* falsy 赋值 */
        break;
      case AssignmentOpKind::NullishAssign:
        builder_.jump_if_nullish(&assign_path); /* nullish 赋值 */
        break;
      default:
        break;
    }
    /* 短路路径：返回旧值（重新读 slot，未写回）。 */
    if (is_upvalue) {
      builder_.load_upvalue(capture);
    } else {
      builder_.load_var(slot);
    }
    builder_.jump(&done);
    /* 赋值路径：求 rhs 写回，值 = rhs。 */
    builder_.bind_label(assign_path);
    if (!compileExpr(*rhs)) return false;
    if (is_upvalue) {
      builder_.store_upvalue(capture);
      builder_.load_upvalue(capture);
    } else {
      builder_.store_var(slot);
      builder_.load_var(slot);
    }
    builder_.bind_label(done);
    return true;
  }

  /* 复合赋值：a op= b -> a = a op b（先读旧值，运算后存回）。 */
  if (node.assign_op != AssignmentOpKind::Assign) {
    BinaryOpKind bop = BinaryOpKind::Add;
    if (!compoundAssignToBinary(node.assign_op, &bop)) {
      fail("not implemented: compound assignment operator");
      return false;
    }
    Opcode op = Opcode::Nop;
    if (!binaryOpcode(bop, &op)) {
      fail("not implemented: compound assignment operator");
      return false;
    }
    if (is_upvalue) {
      builder_.load_upvalue(capture); /* 旧值 */
    } else {
      builder_.load_var(slot);
    }
    if (!compileExpr(*rhs)) return false;
    builder_.push_op(op);
    if (is_upvalue) {
      builder_.store_upvalue(capture); /* 存回（弹栈） */
      builder_.load_upvalue(capture);  /* 赋值表达式值 = 新值 */
    } else {
      builder_.store_var(slot);
      builder_.load_var(slot);
    }
    return true;
  }

  /* 简单赋值：a = expr。赋值表达式值 = rhs。 */
  if (!compileExpr(*rhs)) return false;
  if (is_upvalue) {
    builder_.store_upvalue(capture); /* 弹栈 */
    builder_.load_upvalue(capture);  /* 保留赋值结果在栈顶 */
  } else {
    builder_.store_var(slot);
    builder_.load_var(slot);
  }
  return true;
}

bool Compiler::compileCall(const ASTNode &node) {
  if (node.children.empty()) {
    fail("call without callee");
    return false;
  }
  const ASTNode *callee = node.children[0];
  size_t argc = node.children.size() - 1;

  /* 展开调用（含展开参数）：f(...a, b) / obj.m(...a)。
   * 成员调用保留 this（obj, func），普通调用只需 func。
   * 参数构建到临时数组，CallArray/展开版 CallMethod 展开。
   * 本阶段成员展开调用用 CallArray 近似（this 绑定不完整），
   * 简单 this 场景（obj.m() 无展开）仍走 CallMethod。 */
  bool has_spread = false;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (node.children[i] != nullptr &&
        node.children[i]->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }

  if (callee->type == NodeType::SuperProperty) {
    /* M5.1d-b super.m(args)：this = 当前帧 this（§12.3.5.3）。
     * 发射：LoadThis; SuperLoadProp; args...; CallMethod。
     * 栈 [this, func, args] 与 CallMethod 布局一致。 */
    if (callee->member.optional || callee->children.empty()) {
      fail("not implemented: optional chaining on super");
      return false;
    }
    builder_.load_this();
    if (!callee->member.computed) {
      const ASTNode *prop = callee->children[0];
      if (prop->type != NodeType::Identifier) {
        fail("invalid super property name");
        return false;
      }
      builder_.super_load_prop(prop->name);
    } else {
      if (!compileExpr(*callee->children[0])) return false;
      builder_.super_load_elem();
    }
    if (has_spread) {
      fail("not implemented: spread call on super");
      return false;
    }
    for (size_t i = 1; i < node.children.size(); ++i) {
      if (!compileExpr(*node.children[i])) return false;
    }
    builder_.call_method(static_cast<uint16_t>(argc));
    return true;
  }

  if (callee->type == NodeType::MemberExpression) {
    if (callee->member.optional || callee->children.size() < 2) {
      fail("not implemented: optional chaining in call");
      return false;
    }
    const ASTNode *prop = callee->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member call");
      return false;
    }
    if (has_spread) {
      /* obj.m(...a)：callee 求值（obj.m 值）作普通函数，构建 args
       * 数组后 CallArray。this 绑定为 undefined（CallArray 无 this 槽），
       * 成员展开调用的 this 语义 M5.1e 补全。 */
      if (!compileExpr(*callee)) return false; /* func（obj.m 求值） */
      uint16_t args_slot = scope_->next_slot++;
      builder_.new_array();
      builder_.store_var(args_slot);
      std::vector<const ASTNode *> args(node.children.begin() + 1,
                                        node.children.end());
      if (!compileSpreadArgs(args, args_slot)) return false;
      builder_.load_var(args_slot);
      builder_.call_array();
      return true;
    }
    if (!compileExpr(*callee->children[0])) return false; /* obj */
    builder_.dup();                                        /* obj, obj */
    if (!callee->member.computed) {
      if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      }
      builder_.load_prop(prop->name); /* obj, func */
    } else {
      if (!compileExpr(*prop)) return false; /* obj, obj, key */
      builder_.load_elem();                  /* obj, func */
    }
    for (size_t i = 1; i < node.children.size(); ++i) {
      if (!compileExpr(*node.children[i])) return false;
    }
    builder_.call_method(static_cast<uint16_t>(argc));
    return true;
  }

  /* 普通调用：callee 先入栈，再压 argc 个参数，Call {argc}。 */
  if (!compileExpr(*callee)) return false;
  if (has_spread) {
    /* f(...a, b)：参数构建到数组，CallArray 展开调用。 */
    uint16_t args_slot = scope_->next_slot++;
    builder_.new_array();
    builder_.store_var(args_slot);
    std::vector<const ASTNode *> args(node.children.begin() + 1,
                                      node.children.end());
    if (!compileSpreadArgs(args, args_slot)) return false;
    builder_.load_var(args_slot);
    builder_.call_array();
    return true;
  }
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
  }
  builder_.call(static_cast<uint16_t>(argc));
  return true;
}

/* new 构造（M4.2）：new Foo(args) / new Foo。
 *   ctor, args... -> Construct {argc}
 * new 语义下 this 是新对象，callee 只需求值（成员构造 new a.b() 的
 * this 与 a 无关，直接 LoadProp 取构造器即可，无需保留接收者）。 */
bool Compiler::compileNew(const ASTNode &node) {
  if (node.children.empty()) {
    fail("new without callee");
    return false;
  }
  const ASTNode *callee = node.children[0];
  if (callee->type == NodeType::MemberExpression) {
    /* new a.b(...)：求值 a 并取 b（this 语义不影响 new）。 */
    if (callee->member.optional || callee->children.size() < 2) {
      fail("not implemented: optional chaining in new");
      return false;
    }
    if (!compileExpr(*callee->children[0])) return false;
    const ASTNode *prop = callee->children[1];
    if (prop->type == NodeType::PrivateIdentifier) {
      fail("not implemented: private member in new");
      return false;
    }
    if (!callee->member.computed) {
      if (prop->type != NodeType::Identifier) {
        fail("invalid member property name");
        return false;
      }
      builder_.load_prop(prop->name);
    } else {
      if (!compileExpr(*prop)) return false;
      builder_.load_elem();
    }
  } else {
    if (!compileExpr(*callee)) return false;
  }
  size_t argc = node.children.size() - 1;
  /* 展开 new：new Foo(...a, b) —— 参数构建到数组，ConstructArray 展开。 */
  bool has_spread = false;
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (node.children[i] != nullptr &&
        node.children[i]->type == NodeType::SpreadElement) {
      has_spread = true;
      break;
    }
  }
  if (has_spread) {
    uint16_t args_slot = scope_->next_slot++;
    builder_.new_array();
    builder_.store_var(args_slot);
    std::vector<const ASTNode *> args(node.children.begin() + 1,
                                      node.children.end());
    if (!compileSpreadArgs(args, args_slot)) return false;
    builder_.load_var(args_slot);
    builder_.construct_array();
    return true;
  }
  for (size_t i = 1; i < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
  }
  builder_.construct(static_cast<uint16_t>(argc));
  return true;
}

bool Compiler::compileSequence(const ASTNode &node) {
  /* 逗号序列：逐个求值，保留最后一个。 */
  if (node.children.empty()) {
    builder_.push_op(Opcode::PushUndefined);
    return true;
  }
  for (size_t i = 0; i + 1 < node.children.size(); ++i) {
    if (!compileExpr(*node.children[i])) return false;
    builder_.pop(); /* 丢弃中间值 */
  }
  return compileExpr(*node.children.back());
}

/* -------------------------------------------------------------------------- */
/* 局部变量槽分配与解析                                                        */
/* -------------------------------------------------------------------------- */

uint16_t Compiler::declareLocal(const std::string &name) {
  if (scope_ == nullptr) return 0;
  auto it = scope_->locals.find(name);
  if (it != scope_->locals.end()) return it->second; /* 已声明：复用槽 */
  uint16_t slot = scope_->next_slot++;
  scope_->locals[name] = slot;
  builder_.set_local_name(slot, name); /* M2.5：记录槽名（环境同步） */
  return slot;
}

bool Compiler::resolveLocal(const std::string &name, uint16_t *slot) {
  if (scope_ == nullptr) return false;
  auto it = scope_->locals.find(name);
  if (it == scope_->locals.end()) return false;
  *slot = it->second;
  return true;
}

/* 标识符读取（M2.5）：本函数局部 -> LoadVar；外层局部 -> 捕获 LoadUpvalue；
 * 否则 LoadGlobal。返回 true 表示已发射 load 指令。 */
bool Compiler::emitLoadIdentifier(const std::string &name) {
  uint16_t slot = 0;
  if (resolveLocal(name, &slot)) {
    builder_.load_var(slot);
    return true;
  }

  /* 沿作用域链查找外层函数是否声明该变量。找到则捕获为 upvalue。
   * depth = 外层函数作用域帧层数（0 = 直接外层）。 */
  int depth = 0;
  for (CompilerScope *s = scope_->parent; s != nullptr; s = s->parent, ++depth) {
    auto it = s->locals.find(name);
    if (it != s->locals.end()) {
      uint16_t idx = builder_.add_capture(static_cast<uint16_t>(depth), name);
      builder_.load_upvalue(idx);
      return true;
    }
  }

  builder_.load_global(name);
  return true;
}

/* 赋值目标解析（M2.5）：本函数局部 -> slot；外层局部 -> capture upvalue；
 * 否则返回 false（全局赋值本阶段不支持，见 compileAssignment）。 */
bool Compiler::resolveAssignTarget(const std::string &name, uint16_t *slot,
                                   uint16_t *capture, bool *is_upvalue) {
  *is_upvalue = false;
  if (resolveLocal(name, slot)) return true;

  int depth = 0;
  for (CompilerScope *s = scope_->parent; s != nullptr; s = s->parent, ++depth) {
    auto it = s->locals.find(name);
    if (it != s->locals.end()) {
      *capture = builder_.add_capture(static_cast<uint16_t>(depth), name);
      *is_upvalue = true;
      return true;
    }
  }
  return false; /* 全局 */
}

bool Compiler::isSimpleAssignTarget(const ASTNode &node) const {
  return node.type == NodeType::Identifier;
}

}  // namespace neojs
