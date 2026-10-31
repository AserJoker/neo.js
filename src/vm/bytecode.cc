#include "neojs/vm/bytecode.h"

namespace neojs {
namespace vm {

uint32_t Bytecode::add_number(double n) {
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].kind == ConstEntry::Kind::Number &&
        constants[i].number == n) {
      return static_cast<uint32_t>(i);
    }
  }
  ConstEntry e;
  e.kind = ConstEntry::Kind::Number;
  e.number = n;
  constants.push_back(e);
  return static_cast<uint32_t>(constants.size() - 1);
}

uint32_t Bytecode::add_string(const std::string &s) {
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].kind == ConstEntry::Kind::String && constants[i].str == s) {
      return static_cast<uint32_t>(i);
    }
  }
  ConstEntry e;
  e.kind = ConstEntry::Kind::String;
  e.str = s;
  constants.push_back(e);
  return static_cast<uint32_t>(constants.size() - 1);
}

uint32_t Bytecode::add_boolean(bool b) {
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].kind == ConstEntry::Kind::Boolean &&
        constants[i].boolean == b) {
      return static_cast<uint32_t>(i);
    }
  }
  ConstEntry e;
  e.kind = ConstEntry::Kind::Boolean;
  e.boolean = b;
  constants.push_back(e);
  return static_cast<uint32_t>(constants.size() - 1);
}

uint32_t Bytecode::add_null() {
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].kind == ConstEntry::Kind::Null) {
      return static_cast<uint32_t>(i);
    }
  }
  ConstEntry e;
  e.kind = ConstEntry::Kind::Null;
  constants.push_back(e);
  return static_cast<uint32_t>(constants.size() - 1);
}

uint32_t Bytecode::add_undefined() {
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i].kind == ConstEntry::Kind::Undefined) {
      return static_cast<uint32_t>(i);
    }
  }
  ConstEntry e;
  e.kind = ConstEntry::Kind::Undefined;
  constants.push_back(e);
  return static_cast<uint32_t>(constants.size() - 1);
}

/* ---------- 调试 dump ---------- */

namespace {

const char *opcodeName(Opcode op) {
  switch (op) {
    case Opcode::PushConst: return "PushConst";
    case Opcode::PushUndefined: return "PushUndefined";
    case Opcode::PushNull: return "PushNull";
    case Opcode::PushTrue: return "PushTrue";
    case Opcode::PushFalse: return "PushFalse";
    case Opcode::PushUninitialized: return "PushUninitialized";
    case Opcode::LoadVar: return "LoadVar";
    case Opcode::StoreVar: return "StoreVar";
    case Opcode::DefineVar: return "DefineVar";
    case Opcode::DefineVarGlobal: return "DefineVarGlobal";
    case Opcode::PushScope: return "PushScope";
    case Opcode::PopScope: return "PopScope";
    case Opcode::CreateFunction: return "CreateFunction";
    case Opcode::SetClosure: return "SetClosure";
    case Opcode::Call: return "Call";
    case Opcode::Pos: return "Pos";
    case Opcode::Neg: return "Neg";
    case Opcode::Not: return "Not";
    case Opcode::BitNot: return "BitNot";
    case Opcode::Typeof: return "Typeof";
    case Opcode::Void: return "Void";
    case Opcode::Add: return "Add";
    case Opcode::Sub: return "Sub";
    case Opcode::Mul: return "Mul";
    case Opcode::Div: return "Div";
    case Opcode::Mod: return "Mod";
    case Opcode::Pow: return "Pow";
    case Opcode::BitAnd: return "BitAnd";
    case Opcode::BitOr: return "BitOr";
    case Opcode::BitXor: return "BitXor";
    case Opcode::Shl: return "Shl";
    case Opcode::Sar: return "Sar";
    case Opcode::Shr: return "Shr";
    case Opcode::Eq: return "Eq";
    case Opcode::Ne: return "Ne";
    case Opcode::StrictEq: return "StrictEq";
    case Opcode::StrictNe: return "StrictNe";
    case Opcode::Lt: return "Lt";
    case Opcode::Le: return "Le";
    case Opcode::Gt: return "Gt";
    case Opcode::Ge: return "Ge";
    case Opcode::Jump: return "Jump";
    case Opcode::JumpIfTrue: return "JumpIfTrue";
    case Opcode::JumpIfFalse: return "JumpIfFalse";
    case Opcode::JumpIfNullish: return "JumpIfNullish";
    case Opcode::JumpIfNotNullish: return "JumpIfNotNullish";
    case Opcode::JumpIfTruePop: return "JumpIfTruePop";
    case Opcode::JumpIfFalsePop: return "JumpIfFalsePop";
    case Opcode::Pop: return "Pop";
    case Opcode::Ret: return "Ret";
    case Opcode::Halt: return "Halt";
    case Opcode::EnableOption: return "EnableOption";
    case Opcode::Count: return "?Count";
  }
  return "?";
}

/* 指令操作数文本：LoadVar/StoreVar/DefineVar/SetClosure → 常量池字符串。 */
std::string operandText(const Bytecode &bc, const Instruction &in) {
  switch (in.op) {
    case Opcode::LoadVar:
    case Opcode::StoreVar:
    case Opcode::DefineVar:
    case Opcode::DefineVarGlobal:
    case Opcode::SetClosure:
    case Opcode::EnableOption:
      return " '" + bc.constants[in.arg].str + "'";
    case Opcode::PushConst: {
      const ConstEntry &e = bc.constants[in.arg];
      switch (e.kind) {
        case ConstEntry::Kind::Number:
          return " " + std::to_string(e.number);
        case ConstEntry::Kind::String:
          return " '" + e.str + "'";
        case ConstEntry::Kind::Boolean:
          return e.boolean ? " true" : " false";
        case ConstEntry::Kind::Null:
          return " null";
        case ConstEntry::Kind::Undefined:
          return " undefined";
      }
      break;
    }
    case Opcode::CreateFunction:
      return " #" + std::to_string(in.arg) +
             " (offset=" + std::to_string(bc.functions[in.arg].offset) + ")";
    default:
      return " " + std::to_string(in.arg);
  }
  return "";
}

}  // namespace

std::string dumpBytecode(const Bytecode &bc) {
  std::string out;
  out += "--- bytecode (" + std::to_string(bc.instructions.size()) +
         " instr, " + std::to_string(bc.constants.size()) + " const) ---\n";
  for (size_t i = 0; i < bc.instructions.size(); ++i) {
    const Instruction &in = bc.instructions[i];
    out += "  " + std::to_string(i) + ": ";
    out += opcodeName(in.op);
    out += operandText(bc, in);
    out += "\n";
  }
  if (!bc.functions.empty()) {
    out += "--- functions (" + std::to_string(bc.functions.size()) +
           ") ---\n";
    for (size_t i = 0; i < bc.functions.size(); ++i) {
      const FunctionInfo &f = bc.functions[i];
      out += "  #" + std::to_string(i) + ": offset=" +
             std::to_string(f.offset) + " params=" +
             std::to_string(f.param_count) + "\n";
    }
  }
  return out;
}

}  // namespace vm
}  // namespace neojs
