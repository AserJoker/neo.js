/*
 * vm 层：字节码表示（表达式子集，M4.1）。
 *
 * 架构（docs/architecture.md §5.1 + scope-redesign.md）：
 *   - 字节码前（编译器：AST → 字节码）→ 字节码后（vm + engine）。
 *   - vm 解析字节码，逐条调用 engine 接口完成语义。
 *   - 本文件定义字节码结构：Opcode 枚举 + 常量池 + 指令序列。
 *
 * M4 范围：仅表达式（无函数调用/语句）。指令集是架构文档 §5.1 的子集。
 *
 * 约定：
 *   - 指令 = 单字节 Opcode + 可选立即数（uint32）。
 *   - 常量池：number/string/boolean/null/undefined 等直接值。
 *   - 变量加载用 load_var {name}（name 存常量池，idx 指向 name 字符串）。
 */

#ifndef NEOJS_VM_BYTECODE_H
#define NEOJS_VM_BYTECODE_H

#include <cstdint>
#include <string>
#include <vector>

#include "neojs/core/allocator.h"

namespace neojs {
namespace vm {

/* 操作码（架构文档 §5.1 子集；M5-A 扩语句） */
enum class Opcode : uint8_t {
  /* 常量/加载 */
  PushConst,      /* 压常量池[idx] */
  PushUndefined,  /* 压 undefined */
  PushNull,       /* 压 null */
  PushTrue,       /* 压 true */
  PushFalse,      /* 压 false */
  PushUninitialized, /* 压 TDZ 哨兵（Uninitialized） */
  LoadVar,        /* 加载变量：常量池[idx]=name，沿 scope 链 load */
  StoreVar,       /* 存储变量：常量池[idx]=name，store 语义（改 data 不顶替） */
  DefineVar,      /* 声明变量：常量池[idx]=name，define 到当前 scope（顶替） */
  DefineVarGlobal, /* 声明 var：define 到 var 目标 scope（M5-A=全局，函数后=函数级） */
  PushScope,      /* 进入块作用域：ctx.push_scope() */
  PopScope,       /* 离开块作用域：ctx.pop_scope() */

  /* 一元 */
  Pos,    /* +x */
  Neg,    /* -x */
  Not,    /* !x */
  BitNot, /* ~x */
  Typeof, /* typeof x */
  Void,   /* void x */

  /* 二元（从栈顶取两个操作数，结果压栈） */
  Add,  /* + */
  Sub,  /* - */
  Mul,  /* * */
  Div,  /* / */
  Mod,  /* % */
  Pow,  /* ** */

  /* 位运算 */
  BitAnd, /* & */
  BitOr,  /* | */
  BitXor, /* ^ */
  Shl,    /* << */
  Sar,    /* >> */
  Shr,    /* >>> */

  /* 比较 */
  Eq,        /* == */
  Ne,        /* != */
  StrictEq,  /* === */
  StrictNe,  /* !== */
  Lt,        /* < */
  Le,        /* <= */
  Gt,        /* > */
  Ge,        /* >= */

  /* 逻辑短路（跳转目标为指令序号 offset） */
  Jump,           /* 无条件跳转到 target */
  JumpIfTrue,     /* 栈顶为真 → target（保留栈顶） */
  JumpIfFalse,    /* 栈顶为假 → target（保留栈顶） */
  JumpIfNullish,  /* 栈顶 null/undefined → target（保留栈顶） */
  JumpIfNotNullish, /* 栈顶非 null/undefined → target（保留栈顶） */
  JumpIfTruePop,  /* 栈顶为真 → target（消费栈顶） */
  JumpIfFalsePop, /* 栈顶为假 → target（消费栈顶） */
  Pop,            /* 弹栈 */

  /* 终止 */
  Ret, /* 返回栈顶 */

  Count, /* 指令条数（vm 层 handler 表大小用） */
};

/* 指令：操作码 + 立即数（target/idx/name 索引） */
struct Instruction {
  Opcode op = Opcode::PushUndefined;
  uint32_t arg = 0;

  Instruction() = default;
  Instruction(Opcode o) : op(o) {}
  Instruction(Opcode o, uint32_t a) : op(o), arg(a) {}
};

/* 常量池条目：直接值 */
struct ConstEntry {
  enum class Kind : uint8_t { Number, String, Boolean, Null, Undefined };

  Kind kind = Kind::Undefined;
  double number = 0.0;
  std::string str;
  bool boolean = false;
};

/* 函数体在 module bytecode 中的位置（借用同一 bytecode + 指令偏移）。
 * 每个函数 = 一段连续指令区间 [offset, end)。顶层（Program）为 offset 0。
 * M5-A 先建模型；函数体编译/调用在 M5-B 落地。 */
struct FunctionInfo {
  size_t offset = 0; /* 起始指令偏移 */
  size_t end = 0;    /* 结束指令偏移（不含） */
};

/* 模块字节码：指令 + 常量池 + 函数表（函数借用本 bytecode + offset）。 */
struct Bytecode : core::AllocatedObject {
  std::vector<Instruction> instructions;
  std::vector<ConstEntry> constants;
  std::vector<FunctionInfo> functions; /* 函数表（M5-B 填充） */

  /* 常量池去重添加：number 返回 idx */
  uint32_t add_number(double n);
  uint32_t add_string(const std::string &s);
  uint32_t add_boolean(bool b);
  uint32_t add_null();
  uint32_t add_undefined();

  void emit(Opcode op) { instructions.push_back(Instruction(op)); }
  void emit(Opcode op, uint32_t arg) {
    instructions.push_back(Instruction(op, arg));
  }

  /* 当前指令数（跳转目标用） */
  size_t size() const { return instructions.size(); }

  /* 回填跳转目标：把位置 at 的指令 arg 设为 target */
  void patch_jump(size_t at, size_t target) {
    instructions[at].arg = static_cast<uint32_t>(target);
  }
};

}  // namespace vm
}  // namespace neojs

#endif  // NEOJS_VM_BYTECODE_H
