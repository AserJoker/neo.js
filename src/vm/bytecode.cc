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

}  // namespace vm
}  // namespace neojs
