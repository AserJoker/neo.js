/*
 * 内部 C++ 实现：BytecodeBuilder（发射式字节码构造 API）。
 *
 * 后续 Compiler（AST → Bytecode）复用本类发射指令；VM 消费
 * FunctionBytecode（指令序列 + 常量池 + 局部槽数）。
 */

#include "neojs/compiler/bytecode.h"

#include <algorithm>

namespace neojs {

void BytecodeBuilder::push_op(Opcode op, uint16_t arg, BcLabel *label) {
  Instruction instr;
  instr.op = op;
  instr.arg = arg;
  fn_.code.push_back(instr);

  /* 若带 label 且有效：
   *   - 已绑定（后向跳转）：立即用绑定目标回填当前指令的 arg。
   *   - 未绑定（前向跳转）：记入待回填引用，bind_label 时统一回填。 */
  if (label != nullptr && label->index >= 0 &&
      static_cast<size_t>(label->index) < labels_.size()) {
    LabelEntry &entry = labels_[static_cast<size_t>(label->index)];
    if (entry.bound) {
      fn_.code.back().arg = static_cast<uint16_t>(entry.target);
    } else {
      entry.refs.push_back(fn_.code.size() - 1);
    }
  }
}

void BytecodeBuilder::bind_label(BcLabel &label) {
  if (label.index < 0 || static_cast<size_t>(label.index) >= labels_.size()) {
    return; /* 无效 label：忽略 */
  }
  LabelEntry &entry = labels_[static_cast<size_t>(label.index)];
  entry.bound = true;
  entry.target = fn_.code.size(); /* 当前指令位置即跳转目标 */

  /* 回填所有引用该 label 的跳转指令的 arg。 */
  for (size_t pos : entry.refs) {
    fn_.code[pos].arg = static_cast<uint16_t>(entry.target);
  }
  entry.refs.clear();
}

uint16_t BytecodeBuilder::add_constant(const std::string &s) {
  auto it = std::find(fn_.constants.begin(), fn_.constants.end(), s);
  if (it != fn_.constants.end()) {
    return static_cast<uint16_t>(it - fn_.constants.begin());
  }
  fn_.constants.push_back(s);
  return static_cast<uint16_t>(fn_.constants.size() - 1);
}

}  // namespace neojs
