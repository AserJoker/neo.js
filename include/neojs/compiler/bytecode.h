/*
 * 内部 C++ 实现：字节码格式 + BytecodeBuilder（发射式构造 API）。
 *
 * 设计（架构 §5.1 指令集，M2-VM 首期最小可跑子集）：
 *   - 栈式 VM：指令操作一个显式操作数栈（架构 §5.4）。
 *   - 指令编码：Instruction{Opcode op, uint16_t arg}，定长 4 字节（对齐友好）。
 *   - BytecodeBuilder 提供 push_op(bytecodes, OP_XXX, ...) 发射式 API，
 *     后续 Compiler（AST → Bytecode）直接复用；label 支持回填跳转目标。
 *   - 常量池：字符串常量按值去重，指令引用池索引。
 *
 * label 机制：BcLabel 只是 builder 内部 label 表索引（int），按值拷贝安全；
 * builder 记录每个 label 的待回填指令位置，bind_label 时统一回填。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_COMPILER_BYTECODE_H
#define NEOJS_COMPILER_BYTECODE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace neojs {

/* -------------------------------------------------------------------------- */
/* 操作码（架构 §5.1 首期子集：常量/加载/算术/比较/跳转/函数/栈操作）       */
/* -------------------------------------------------------------------------- */

enum class Opcode : uint8_t {
  /* 常量/加载 */
  PushConst,       /* 压入常量池值：PushConst {pool_index} */
  PushBigInt,      /* 压入 BigInt 常量：PushBigInt {pool_index}（M4.3） */
  PushUndefined,   /* 压入 undefined */
  PushNull,        /* 压入 null */
  PushTrue,        /* 压入 true */
  PushFalse,       /* 压入 false */
  PushInt,         /* 压入小整数：PushInt {value} */
  LoadVar,         /* 局部变量 -> 栈顶：LoadVar {slot} */
  StoreVar,        /* 栈顶 -> 局部变量：StoreVar {slot} */
  LoadGlobal,      /* 全局对象属性 -> 栈顶：LoadGlobal {name_index} */

  /* 栈操作 */
  Pop,             /* 弹出栈顶（临时值清理） */
  Dup,             /* 复制栈顶（成员调用 this 保留）：Dup */

  /* 算术 */
  Add,             /* a + b（含字符串拼接） */
  Sub,
  Mul,
  Div,
  Mod,             /* % */
  Pow,             /* ** */
  Neg,             /* 一元负号：栈顶取负 */
  Not,             /* 一元逻辑非：!x */
  Typeof,          /* typeof 栈顶 */

  /* 比较 */
  Eq,              /* == */
  StrictEq,        /* === */
  Lt,              /* < */
  Le,              /* <= */
  Gt,              /* > */
  Ge,              /* >= */
  Ne,              /* != */
  StrictNe,        /* !== */

  /* 位运算 */
  BitAnd,          /* & */
  BitOr,           /* | */
  BitXor,          /* ^ */
  LShift,          /* << */
  RShift,          /* >> */
  URShift,         /* >>> */

  /* 跳转 */
  Jump,            /* 无条件跳转：Jump {target} */
  JumpIfFalse,     /* 栈顶为 false 则跳转：JumpIfFalse {target}（弹栈） */

  /* 函数/调用 */
  Call,            /* 调用栈顶函数：Call {argc}（弹出函数+argc 参数） */
  CallMethod,      /* 成员调用：obj, func, args... -> 结果（M4.1，this=obj） */
  Construct,       /* new 构造：ctor, args... -> 实例（M4.2，this=新对象） */
  Ret,             /* 返回栈顶值，弹出本帧 */

  /* 闭包（M2.5） */
  MakeFunction,    /* 从嵌套函数表实例化函数并绑定当前环境：MakeFunction {index} */
  LoadUpvalue,     /* 捕获的外层变量 -> 栈顶：LoadUpvalue {capture_index} */
  StoreUpvalue,    /* 栈顶 -> 捕获的外层变量：StoreUpvalue {capture_index} */

  /* 对象/数组（M3） */
  NewObject,       /* 压入新空对象：NewObject（arg 保留，原型 Object.prototype） */
  NewArray,        /* 压入新数组：NewArray（arg 保留，空 elements） */
  LoadProp,        /* obj.key -> 栈顶（替换 obj）：LoadProp {key_index} */
  StoreProp,       /* obj, val -> 栈顶 val（obj.key = val）：StoreProp {key_index} */
  LoadElem,        /* obj, key -> 栈顶（替换 obj）：LoadElem（运行期 ToPropertyKey） */
  StoreElem,       /* obj, key, val -> 栈顶 val（obj[key] = val）：StoreElem */

  /* 调试/输出 */
  Print,           /* 弹出栈顶并输出（--print 观测） */
  Nop,             /* 空操作（label 占位） */

  /* 迭代器（M4.4f，§7.4）——追加在末尾，保持既有 C API opcode 数值稳定 */
  GetIterator,     /* 栈顶对象 -> 迭代器（GetIterator，替换 obj） */
  IteratorStep,    /* 栈顶 iterator -> [value, done]（done 在栈顶，§7.4.5） */
  JumpIfTrue,      /* 栈顶为 true 则跳转：JumpIfTrue {target}（弹栈，M4.4f for-of） */
  JumpIfUndefined, /* 栈顶为 undefined 则跳转：JumpIfUndefined {target}（弹栈，M5.1a 解构默认值） */
  RestToArray,     /* iterable, start -> 剩余元素数组（M5.1a rest/展开，§7.4） */

  /* M5.1a 默认参数/rest 参数/展开（追加末尾，保持 opcode 数值稳定） */
  RestParam,       /* 收集剩余调用参数：RestParam {k} -> 压入 args[k..] 新数组（M5.1a rest 参数） */
  AppendElem,      /* 弹栈值追加到槽内数组末尾：AppendElem {slot}（M5.1a 数组展开） */
  AppendSpread,    /* 弹栈可迭代对象，其元素追加到槽内数组：AppendSpread {slot}（M5.1a 展开） */
  CallArray,       /* [func, args_array] -> 以数组元素为参数调用（M5.1a 展开调用） */
  ConstructArray,  /* [ctor, args_array] -> 以数组元素为参数构造（M5.1a 展开 new） */
  CopyObjectProps, /* 弹栈源对象，own enumerable 字符串属性复制进槽内对象：CopyObjectProps {slot}（M5.1a 对象展开） */
  JumpIfNullish,   /* 栈顶为 null 或 undefined 则跳转：JumpIfNullish {target}（弹栈，M5.1b ??= 短路） */

  /* M5.1c 对象字面量：计算键 + getter/setter（追加末尾，保持 opcode 数值稳定） */
  DefineProp,      /* obj, key, val -> 栈顶 val（obj[key]=val 定义 own 属性，key 运行期 ToPropertyKey）：DefineProp（M5.1c 计算键） */
  DefineAccessor,  /* obj, key, fn -> 栈顶 fn（定义访问器属性，arg=0 getter/1 setter）：DefineAccessor {getset}（M5.1c） */
  LoadThis,        /* 当前帧 this -> 栈顶：LoadThis（M5.1c 访问器 this 绑定） */

  /* M5.1d-a class：原型链设置（追加末尾，保持 opcode 数值稳定） */
  SetPrototype,    /* obj, proto -> 设置 obj.[[Prototype]]=proto，压回 obj：SetPrototype（M5.1d-a extends） */

  /* M5.1d-b class super：HomeObject 设置 + super 属性读取（追加末尾，保持 opcode 数值稳定） */
  SetHomeObject,   /* obj, fn -> 设置 fn.[[HomeObject]]=obj，压回 fn：SetHomeObject（M5.1d-b super） */
  SuperLoadProp,   /* super.key -> 栈顶：从 HomeObject.[[Prototype]] 读 key，访问器 this=当前帧 this（M5.1d-b） */
  SuperLoadElem,   /* super[expr] -> 栈顶：从 HomeObject.[[Prototype]] 读计算键，访问器 this=当前帧 this（M5.1d-b） */
};

/* -------------------------------------------------------------------------- */
/* 指令与函数字节码                                                        */
/* -------------------------------------------------------------------------- */

/* 一条指令：opcode + 立即数（定长编码，对齐友好）。 */
struct Instruction {
  Opcode op;
  uint16_t arg = 0;
};

/* 常量池条目：数字/字符串/BigInt（PushConst/LoadGlobal/PushBigInt 经索引引用）。 */
struct Constant {
  bool is_number = false; /* true = number 载荷；false = 字符串载荷 */
  bool is_bigint = false; /* true = BigInt 载荷（M4.3：is_number 与 str 无效） */
  double number = 0.0;
  std::string str;
  std::string bigint_digits; /* BigInt 十进制数字串（无符号，M4.3） */
  bool bigint_neg = false;   /* BigInt 符号位（M4.3） */
};

/* 捕获的外层变量（M2.5 闭包）：编译期静态解析。
 * depth  = 沿环境链上跳层数（0 = 当前函数的外层一层）；
 * name   = 变量名（运行时在目标环境的绑定表中查找）。
 * LoadUpvalue/StoreUpvalue 以 captures 下标引用。 */
struct UpvalueCapture {
  uint16_t depth = 0;
  std::string name;
};

/* 函数级字节码（架构 §4.3：每个函数一个 FunctionBytecode）。 */
struct FunctionBytecode {
  std::vector<Instruction> code;      /* 指令序列 */
  std::vector<Constant> constants;    /* 常量池（数字/字符串） */
  std::string name;                   /* 函数名（可空） */
  uint16_t numLocals = 0;             /* 局部变量槽数 */
  std::vector<std::string> locals;    /* 槽号 -> 变量名（M2.5 环境同步用，空串=无名） */
  std::vector<UpvalueCapture> captures; /* 捕获的外层变量列表（M2.5） */
  std::vector<FunctionBytecode> nested; /* 嵌套函数字节码表（M2.5，MakeFunction 索引） */
  /* class（M5.1d-a）：构造器/方法标志。MakeFunction 实例化时据此设置
   * 构造器（is_constructor -> setConstructor + Strict）与严格模式
   * （is_method -> Strict；class 体一律严格模式，§15.7.2.1）。 */
  bool is_constructor = false; /* class 构造器（可 new，Strict） */
  bool is_method = false;      /* class 方法（Strict，M5.1d-b super 需用） */
  /* M5.1d-b 箭头函数：super 上下文继承（箭头函数内 super 指向外层
   * 方法的 HomeObject）。MakeFunction 据此复制外层 self 的 HomeObject。 */
  bool is_arrow = false;
};

/* -------------------------------------------------------------------------- */
/* BytecodeBuilder（发射式构造 API）                                          */
/* -------------------------------------------------------------------------- */

/* label 句柄：builder 内部 label 表索引（int），按值拷贝安全。
 * index == -1 表示未分配（无效 label）。 */
struct BcLabel {
  int32_t index = -1;
};

class BytecodeBuilder {
 public:
  BytecodeBuilder() = default;
  explicit BytecodeBuilder(const std::string &fn_name) { fn_.name = fn_name; }

  /* 发射一条指令。arg 为立即数（常量池索引/槽位/跳转目标等）。
   * label 非空且有效时，将当前指令位置记为该 label 的待回填引用
   * （bind_label 时回填 arg = 目标位置）。 */
  void push_op(Opcode op, uint16_t arg = 0, BcLabel *label = nullptr);

  /* 便捷发射（与架构 §5.1 命名对应）。 */
  void push_const(const std::string &s) {
    push_op(Opcode::PushConst, add_constant(s));
  }
  void push_number(double d) {
    push_op(Opcode::PushConst, add_number(d));
  }
  void push_bigint(const std::string &digits, bool neg) {
    push_op(Opcode::PushBigInt, add_bigint(digits, neg));
  }
  void push_int(int16_t v) { push_op(Opcode::PushInt, static_cast<uint16_t>(v)); }
  void load_var(uint16_t slot) { push_op(Opcode::LoadVar, slot); }
  void store_var(uint16_t slot) { push_op(Opcode::StoreVar, slot); }
  void load_global(const std::string &name) {
    push_op(Opcode::LoadGlobal, add_constant(name));
  }
  void pop() { push_op(Opcode::Pop); }
  void dup() { push_op(Opcode::Dup); }
  void get_iterator() { push_op(Opcode::GetIterator); }
  void iterator_step() { push_op(Opcode::IteratorStep); }
  void call(uint16_t argc) { push_op(Opcode::Call, argc); }
  void call_method(uint16_t argc) { push_op(Opcode::CallMethod, argc); }
  void construct(uint16_t argc) { push_op(Opcode::Construct, argc); }
  void jump(BcLabel *label) { push_op(Opcode::Jump, 0, label); }
  void jump_if_false(BcLabel *label) { push_op(Opcode::JumpIfFalse, 0, label); }
  void jump_if_true(BcLabel *label) { push_op(Opcode::JumpIfTrue, 0, label); }
  void jump_if_undefined(BcLabel *label) { push_op(Opcode::JumpIfUndefined, 0, label); }
  void jump_if_nullish(BcLabel *label) { push_op(Opcode::JumpIfNullish, 0, label); }
  void rest_to_array() { push_op(Opcode::RestToArray); }
  /* M5.1a 默认参数/rest/展开。 */
  void rest_param(uint16_t k) { push_op(Opcode::RestParam, k); }
  void append_elem(uint16_t slot) { push_op(Opcode::AppendElem, slot); }
  void append_spread(uint16_t slot) { push_op(Opcode::AppendSpread, slot); }
  void call_array() { push_op(Opcode::CallArray); }
  void construct_array() { push_op(Opcode::ConstructArray); }
  void copy_object_props(uint16_t slot) { push_op(Opcode::CopyObjectProps, slot); }
  void ret() { push_op(Opcode::Ret); }
  void print() { push_op(Opcode::Print); }

  /* 闭包（M2.5）：从嵌套函数表实例化 / 读写捕获变量。 */
  void make_function(uint16_t idx) { push_op(Opcode::MakeFunction, idx); }
  void load_upvalue(uint16_t idx) { push_op(Opcode::LoadUpvalue, idx); }
  void store_upvalue(uint16_t idx) { push_op(Opcode::StoreUpvalue, idx); }

  /* 对象/数组（M3）：字面量构造 + 属性读写。 */
  void new_object() { push_op(Opcode::NewObject); }
  void new_array() { push_op(Opcode::NewArray); }
  void load_prop(const std::string &key) {
    push_op(Opcode::LoadProp, add_constant(key));
  }
  void store_prop(const std::string &key) {
    push_op(Opcode::StoreProp, add_constant(key));
  }
  void load_elem() { push_op(Opcode::LoadElem); }
  void store_elem() { push_op(Opcode::StoreElem); }

  /* M5.1c：计算键数据属性定义 + 访问器属性定义。 */
  void define_prop() { push_op(Opcode::DefineProp); }
  void define_accessor(uint16_t getset) { push_op(Opcode::DefineAccessor, getset); }
  /* M5.1c：读取当前帧 this。 */
  void load_this() { push_op(Opcode::LoadThis); }
  /* M5.1d-a：设置对象 [[Prototype]]（obj, proto -> obj）。 */
  void set_prototype() { push_op(Opcode::SetPrototype); }
  /* M5.1d-b：设置函数 [[HomeObject]]（obj, key, fn -> obj, key, fn，super 用）。
   * 构造器/static 场景用 self 变体（obj, fn -> fn）。 */
  void set_home_object() { push_op(Opcode::SetHomeObject, 0); }
  void set_home_object_self() { push_op(Opcode::SetHomeObject, 1); }
  /* M5.1d-b：super.key / super[expr]（从 HomeObject.[[Prototype]] 读取）。 */
  void super_load_prop(const std::string &key) {
    push_op(Opcode::SuperLoadProp, add_constant(key));
  }
  void super_load_elem() { push_op(Opcode::SuperLoadElem); }

  /* 注册捕获变量（去重），返回捕获下标。 */
  uint16_t add_capture(uint16_t depth, const std::string &name) {
    for (size_t i = 0; i < fn_.captures.size(); ++i) {
      if (fn_.captures[i].depth == depth && fn_.captures[i].name == name) {
        return static_cast<uint16_t>(i);
      }
    }
    UpvalueCapture c;
    c.depth = depth;
    c.name = name;
    fn_.captures.push_back(c);
    return static_cast<uint16_t>(fn_.captures.size() - 1);
  }

  /* 注册嵌套函数字节码（M2.5），返回函数表下标（MakeFunction 索引）。 */
  uint16_t add_nested(FunctionBytecode fn) {
    fn_.nested.push_back(std::move(fn));
    return static_cast<uint16_t>(fn_.nested.size() - 1);
  }

  /* label：创建 + 绑定（回填所有引用该 label 的跳转指令）。 */
  BcLabel new_label() {
    BcLabel l;
    l.index = static_cast<int32_t>(labels_.size());
    labels_.push_back(LabelEntry{});
    return l;
  }
  void bind_label(BcLabel &label);

  /* 常量池：按值去重，返回池索引。 */
  uint16_t add_constant(const std::string &s);
  uint16_t add_number(double d);
  uint16_t add_bigint(const std::string &digits, bool neg);

  /* 局部变量槽数（Compiler 设置）。 */
  void set_num_locals(uint16_t n) { fn_.numLocals = n; }

  /* 记录槽号对应的变量名（M2.5 环境同步：参数/局部变量入环境用）。 */
  void set_local_name(uint16_t slot, const std::string &name) {
    if (slot >= fn_.locals.size()) fn_.locals.resize(slot + 1);
    fn_.locals[slot] = name;
  }

  /* 产出函数字节码（移动语义）。 */
  FunctionBytecode build() { return std::move(fn_); }

  /* 调试：当前指令数 / 指令序列只读访问。 */
  size_t size() const { return fn_.code.size(); }
  const std::vector<Instruction> &code() const { return fn_.code; }

 private:
  struct LabelEntry {
    bool bound = false;         /* 是否已绑定 */
    size_t target = 0;          /* 绑定目标（指令位置） */
    std::vector<size_t> refs;   /* 引用该 label 的待回填指令位置 */
  };

  FunctionBytecode fn_;
  std::vector<LabelEntry> labels_; /* label 表（new_label 分配） */
};

}  // namespace neojs

#endif  // NEOJS_COMPILER_BYTECODE_H
