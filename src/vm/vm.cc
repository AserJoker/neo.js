#include "neojs/vm/vm.h"

#include <cmath>
#include <limits>

#include "neojs/engine/engine.h"

namespace neojs {
namespace vm {

namespace {

using engine::add;
using engine::bit_and;
using engine::bit_or;
using engine::bitwise_not;
using engine::bit_xor;
using engine::div;
using engine::eq;
using engine::ge;
using engine::gt;
using engine::le;
using engine::logical_not;
using engine::lt;
using engine::lshift;
using engine::mod;
using engine::mul;
using engine::ne;
using engine::pow;
using engine::rshift;
using engine::strict_eq;
using engine::strict_ne;
using engine::sub;
using engine::to_boolean;
using engine::typeof_value;
using engine::unary_minus;
using engine::unary_plus;
using engine::urshift;
using engine::void_value;

}  // namespace

Value *Vm::run(const Bytecode &bc) {
  stack_.clear();
  const auto &insns = bc.instructions;
  const auto &consts = bc.constants;
  size_t pc = 0;

  while (pc < insns.size()) {
    const Instruction &instr = insns[pc];
    switch (instr.op) {
      case Opcode::PushConst: {
        const ConstEntry &e = consts[instr.arg];
        switch (e.kind) {
          case ConstEntry::Kind::Number:
            push(engine::create_number(ctx_, e.number));
            break;
          case ConstEntry::Kind::String:
            push(engine::create_string(ctx_, e.str));
            break;
          case ConstEntry::Kind::Boolean:
            push(engine::create_boolean(ctx_, e.boolean));
            break;
          case ConstEntry::Kind::Null:
            push(engine::create_null(ctx_));
            break;
          case ConstEntry::Kind::Undefined:
            push(engine::create_undefined(ctx_));
            break;
        }
        ++pc;
        break;
      }

      case Opcode::PushUndefined:
        push(engine::create_undefined(ctx_));
        ++pc;
        break;
      case Opcode::PushNull:
        push(engine::create_null(ctx_));
        ++pc;
        break;
      case Opcode::PushTrue:
        push(engine::create_boolean(ctx_, true));
        ++pc;
        break;
      case Opcode::PushFalse:
        push(engine::create_boolean(ctx_, false));
        ++pc;
        break;

      case Opcode::LoadVar: {
        const std::string &name = consts[instr.arg].str;
        /* 全局内置常量：undefined / NaN / Infinity（恒存在，不依赖 scope） */
        if (name == "undefined") {
          push(engine::create_undefined(ctx_));
          ++pc;
          break;
        }
        if (name == "NaN") {
          push(engine::create_number(ctx_, std::nan("")));
          ++pc;
          break;
        }
        if (name == "Infinity") {
          push(engine::create_number(ctx_, std::numeric_limits<double>::infinity()));
          ++pc;
          break;
        }
        Value *v = ctx_.load(name);
        if (v == nullptr) {
          /* 未定义变量：ReferenceError（异常 value） */
          push(engine::create_exception(ctx_, "ReferenceError: " + name +
                                                 " is not defined"));
          return pop();
        }
        push(v); /* 借用值：变量本身（scope 持有），栈不计数 */
        ++pc;
        break;
      }

      /* ---- 一元 ---- */
      case Opcode::Pos: {
        Value *v = pop();
        push(unary_plus(ctx_, v));
        ++pc;
        break;
      }
      case Opcode::Neg: {
        Value *v = pop();
        push(unary_minus(ctx_, v));
        ++pc;
        break;
      }
      case Opcode::Not: {
        Value *v = pop();
        push(logical_not(ctx_, v));
        ++pc;
        break;
      }
      case Opcode::BitNot: {
        Value *v = pop();
        push(bitwise_not(ctx_, v));
        ++pc;
        break;
      }
      case Opcode::Typeof: {
        Value *v = pop();
        push(typeof_value(ctx_, v));
        ++pc;
        break;
      }
      case Opcode::Void: {
        Value *v = pop();
        push(void_value(ctx_, v));
        ++pc;
        break;
      }

      /* ---- 二元 ---- */
      case Opcode::Add: {
        Value *b = pop();
        Value *a = pop();
        push(add(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Sub: {
        Value *b = pop();
        Value *a = pop();
        push(sub(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Mul: {
        Value *b = pop();
        Value *a = pop();
        push(mul(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Div: {
        Value *b = pop();
        Value *a = pop();
        push(div(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Mod: {
        Value *b = pop();
        Value *a = pop();
        push(mod(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Pow: {
        Value *b = pop();
        Value *a = pop();
        push(pow(ctx_, a, b));
        ++pc;
        break;
      }

      /* ---- 位运算 ---- */
      case Opcode::BitAnd: {
        Value *b = pop();
        Value *a = pop();
        push(bit_and(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::BitOr: {
        Value *b = pop();
        Value *a = pop();
        push(bit_or(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::BitXor: {
        Value *b = pop();
        Value *a = pop();
        push(bit_xor(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Shl: {
        Value *b = pop();
        Value *a = pop();
        push(lshift(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Sar: {
        Value *b = pop();
        Value *a = pop();
        push(rshift(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Shr: {
        Value *b = pop();
        Value *a = pop();
        push(urshift(ctx_, a, b));
        ++pc;
        break;
      }

      /* ---- 比较 ---- */
      case Opcode::Eq: {
        Value *b = pop();
        Value *a = pop();
        push(eq(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Ne: {
        Value *b = pop();
        Value *a = pop();
        push(ne(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::StrictEq: {
        Value *b = pop();
        Value *a = pop();
        push(strict_eq(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::StrictNe: {
        Value *b = pop();
        Value *a = pop();
        push(strict_ne(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Lt: {
        Value *b = pop();
        Value *a = pop();
        push(lt(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Le: {
        Value *b = pop();
        Value *a = pop();
        push(le(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Gt: {
        Value *b = pop();
        Value *a = pop();
        push(gt(ctx_, a, b));
        ++pc;
        break;
      }
      case Opcode::Ge: {
        Value *b = pop();
        Value *a = pop();
        push(ge(ctx_, a, b));
        ++pc;
        break;
      }

      /* ---- 逻辑短路 ---- */
      case Opcode::Jump: {
        pc = instr.arg;
        break;
      }
      case Opcode::JumpIfTrue: {
        Value *v = top();
        bool t = static_cast<core::BooleanData *>(
                     engine::to_boolean(ctx_, v)->data)
                     ->value;
        pc = t ? instr.arg : pc + 1;
        break;
      }
      case Opcode::JumpIfFalse: {
        Value *v = top();
        bool t = static_cast<core::BooleanData *>(
                     engine::to_boolean(ctx_, v)->data)
                     ->value;
        pc = !t ? instr.arg : pc + 1;
        break;
      }
      case Opcode::JumpIfNullish: {
        Value *v = top();
        bool nullish = v->kind == core::ValueKind::Null ||
                       v->kind == core::ValueKind::Undefined;
        pc = nullish ? instr.arg : pc + 1;
        break;
      }
      case Opcode::JumpIfNotNullish: {
        Value *v = top();
        bool nullish = v->kind == core::ValueKind::Null ||
                       v->kind == core::ValueKind::Undefined;
        pc = !nullish ? instr.arg : pc + 1;
        break;
      }
      case Opcode::Pop: {
        pop();
        ++pc;
        break;
      }

      case Opcode::Ret:
        return pop();
    }
  }

  /* 自然结束：返回栈顶（若空则 undefined） */
  if (stack_.empty()) return engine::create_undefined(ctx_);
  return top();
}

}  // namespace vm
}  // namespace neojs
