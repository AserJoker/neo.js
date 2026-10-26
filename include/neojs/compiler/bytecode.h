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

  /* M5.1d-c1 super() 构造调用：args... -> 基类构造器执行（追加末尾，保持 opcode 数值稳定） */
  SuperCall,       /* 调用 HomeObject.[[Prototype]]（基类构造器）于当前帧 this：SuperCall {argc}（M5.1d-c1） */

  /* M5.1d-c2 私有字段：obj -> 值 / obj, val -> val（追加末尾，保持 opcode 数值稳定） */
  LoadPrivate,     /* 读私有字段：receiver -> 值：LoadPrivate {name_index}（M5.1d-c2） */
  StorePrivate,    /* 写私有字段：receiver, val -> val：StorePrivate {name_index}（M5.1d-c2） */
  HasPrivate,      /* 私有字段存在性：#x in obj：obj, name -> bool：HasPrivate {name_index}（M5.1d-c2） */
  InitPrivate,     /* 构造器初始化私有字段槽：receiver, val -> val（无槽则创建）：InitPrivate {name_index}（M5.1d-c2） */

  /* M5.1d-d 派生类默认构造器：以数组元素为参数调用基类构造器于当前帧 this */
  SuperCallArray,  /* [args_array] -> 基类构造器执行（展开 super(...args)，M5.1d-d） */

  /* M5.1e 运算符补全（追加末尾，保持 opcode 数值稳定） */
  In,              /* x in y -> bool（in 运算符，§7.3.20 HasProperty） */
  Instanceof,      /* x instanceof y -> bool（instanceof 运算符，§7.3.22） */
  ToString,        /* 一元：x -> ToString(x)（§7.1.17，模板串插值） */
  ToNumber,        /* 一元 +：x -> ToNumber(x)（§7.1.4，BigInt/Symbol -> TypeError） */
  BitNot,          /* 一元 ~：x -> ~x（§13.5.5，ToNumeric 后按位非） */
  DeleteProp,      /* delete obj.prop：obj -> bool：DeleteProp {key_index}（M5.1e delete） */
  DeleteElem,      /* delete obj[expr]：obj, key -> bool（M5.1e delete 计算键） */
  Increment,       /* 一元 ++/--：x -> x±1（§13.4.4，ToNumeric 后 ±1n/±1，arg=0 ++ / arg=1 --） */

  /* M5.1e-c for-in（§14.7.4，追加末尾，保持 opcode 数值稳定） */
  EnumerateKeys,   /* 栈顶对象 -> 键数组（ToObject + 沿原型链可枚举字符串键，§7.3.24） */
  NextKey,         /* 栈顶键数组 -> 弹出数组，压最后一个键（并移除）或 undefined（空） */

  /* M5.1e-d 对象 rest 解构（§13.15.5，追加末尾，保持 opcode 数值稳定） */
  RestObjectProps, /* [src, exclude_arr] -> 复制 src 中不在 exclude 的 own enumerable 属性到槽内目标：RestObjectProps {target_slot} */

  /* M5.1e-f 异常处理：throw + finally 重抛（追加末尾，保持 opcode 数值稳定） */
  Throw,           /* 栈顶值作为 JS 异常抛出（§14.15）：Throw（弹栈，异常传播） */
  RethrowPending,  /* finally 末尾：若当前有挂起异常则重新抛出（§14.16.4） */

  /* M5.1e-g 隐式全局赋值（追加末尾，保持 opcode 数值稳定） */
  StoreGlobal,     /* 栈顶 -> 全局对象属性：StoreGlobal {name_index}（非严格隐式全局） */

  /* M5.1e-h 成员展开调用（追加末尾，保持 opcode 数值稳定） */
  CallMethodArray, /* [obj, func, args_array] -> 以数组元素为参数调用，this=obj（M5.1e-h 展开成员调用） */

  /* M5.2 模块系统（追加末尾，保持 opcode 数值稳定） */
  LoadImport,      /* 导入绑定读取：压入源模块导出绑定值（M5.2a 选项 A 动态查表） */
  StoreImport,     /* 导入绑定赋值：immutable binding -> TypeError（M5.2a） */

  /* M5.2c import.meta（追加末尾，保持 opcode 数值稳定） */
  LoadImportMeta,  /* 当前模块 import.meta 对象 -> 栈顶：沿 env 链查找 "__import_meta__" 绑定（M5.2c） */

  /* M5.2d 命名空间导入（追加末尾，保持 opcode 数值稳定） */
  LoadImportNamespace, /* import * as ns：压入源模块 namespace 镜像对象（M5.2d） */

  /* M5.4 async/await（追加末尾，保持 opcode 数值稳定） */
  Await,           /* await x：栈顶值 PromiseResolve 接合，未决则挂起当前 async 帧（M5.4） */

  /* M5.5a generator/yield（追加末尾，保持 opcode 数值稳定）
   * §14.4 / §27.5.3.2 GeneratorYield。
   * 栈顶 = yield 值（裸 yield 时为 undefined）。挂起当前 generator 帧，
   * 把值返回给 next() 调用方（包装为 {value, done:false}）；恢复时栈顶替换
   * 为 next()/throw() 传入的值（throw 路径走异常派发）。 */
  Yield,
  /* M5.5a-4 yield* 委托（§14.4 / §27.5.3.3 GeneratorYield + IteratorStep）。
   * 栈顶 = inner iterable（数组/generator/迭代器对象）。VM 内部驱动 inner
   * iterator.next()，逐个把 value 经本 generator 挂起/恢复转发给外层 next()；
   * inner done 后压 inner 返回值到栈继续外层。挂起期间委托状态存
   * AsyncFrame.delegate_iterator（GC 引用边）。throw/return 恢复时转发给
   * inner iterator.throw/return（§27.5.3.3 step 6-7）。 */
  YieldDelegate,
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

/* M5.1e-f try 异常保护表条目（§14.16）：
 * try_start/try_end  保护区间（try 块指令范围，异常在 [start, end) 内命中）。
 * catch_start/catch_end  catch 块区间：catch 内再抛的异常须执行 finally
 *                      （挂起后跳 finally），异常在 [catch_start, catch_end) 内命中。
 * finally_start      无论正常/异常都执行的 finally 入口（无 finally 为 kNoHandler）。
 * catch_slot         catch 参数绑定槽（catch{} 无参数为 kNoCatchSlot）。
 * finally_end        finally 块结束后继续执行点（正常路径汇聚）。
 * 语义：
 *   - 异常在 try 区间：有 catch -> 绑定 catch_slot + 跳 catch_start（消费）；
 *     无 catch 有 finally -> 挂起 + 跳 finally_start，finally 后 RethrowPending 重抛。
 *   - 异常在 catch 区间（catch 再抛）：有 finally -> 挂起 + 跳 finally_start。
 *   - 异常在 finally 区间：不命中（finally 内新异常直接传播，覆盖原异常）。 */
struct TryEntry {
  static constexpr uint32_t kNoHandler = 0xFFFFFFFFu;
  static constexpr uint16_t kNoCatchSlot = 0xFFFFu;
  uint32_t try_start = 0;
  uint32_t try_end = 0;
  uint32_t catch_start = kNoHandler;
  uint32_t catch_end = kNoHandler;
  uint32_t finally_start = kNoHandler;
  uint32_t finally_end = kNoHandler;
  uint16_t catch_slot = kNoCatchSlot;
};

/* 捕获的外层变量（M2.5 闭包）：编译期静态解析。
 * depth  = 沿环境链上跳层数（0 = 当前函数的外层一层）；
 * name   = 变量名（运行时在目标环境的绑定表中查找）。
 * LoadUpvalue/StoreUpvalue 以 captures 下标引用。 */
struct UpvalueCapture {
  uint16_t depth = 0;
  std::string name;
};

/* 模块导入绑定（M5.2a，选项 A 动态查表）：
 * local_name  本模块内的绑定名（import {a as b} 的 b）；
 * specifier   源模块标识（运行期经 ctx->findModule 查记录）；
 * export_name 源模块导出名（import {a as b} 的 a）。
 * LoadImport/StoreImport 以本表下标引用。
 * 运行期：findModule(specifier)->findExport(export_name)->env getBinding。 */
struct ModuleImportEntry {
  std::string local_name;
  std::string specifier;
  std::string export_name;
  bool is_namespace = false; /* M5.2d：import * as ns（LoadImportNamespace） */
};

/* 函数级字节码（架构 §4.3：每个函数一个 FunctionBytecode）。 */
struct FunctionBytecode {
  std::vector<Instruction> code;      /* 指令序列 */
  std::vector<Constant> constants;    /* 常量池（数字/字符串） */
  std::string name;                   /* 函数名（可空） */
  uint16_t numLocals = 0;             /* 局部变量槽数 */
  std::vector<std::string> locals;    /* 槽号 -> 变量名（M2.5 环境同步用，空串=无名） */
  std::vector<UpvalueCapture> captures; /* 捕获的外层变量列表（M2.5） */
  std::vector<ModuleImportEntry> imports; /* 模块导入绑定表（M5.2a，LoadImport 索引） */
  std::vector<FunctionBytecode> nested; /* 嵌套函数字节码表（M2.5，MakeFunction 索引） */
  /* class（M5.1d-a）：构造器/方法标志。MakeFunction 实例化时据此设置
   * 构造器（is_constructor -> setConstructor + Strict）与严格模式
   * （is_method -> Strict；class 体一律严格模式，§15.7.2.1）。 */
  bool is_constructor = false; /* class 构造器（可 new，Strict） */
  bool is_method = false;      /* class 方法（Strict，M5.1d-b super 需用） */
  /* M5.1d-b 箭头函数：super 上下文继承（箭头函数内 super 指向外层
   * 方法的 HomeObject）。MakeFunction 据此复制外层 self 的 HomeObject。 */
  bool is_arrow = false;
  /* M5.1d-c2 私有字段：本函数（class 构造器/方法）可访问的私有字段全名
   * 表（含 '#' 前缀）。MakeFunction 实例化时原样复制。LoadPrivate/
   * StorePrivate/HasPrivate 以该表下标引用字段名（构造器与方法的表
   * 顺序一致，编译期由 class 元素收集顺序保证）。 */
  std::vector<std::string> private_names;
  /* M5.1e-f try 异常保护表（§14.16）：编译期由 try/catch/finally 生成。 */
  std::vector<TryEntry> try_entries;
  /* M5.4 async 函数：async 标记（VM 调用时创建 Promise + await 挂起恢复）。 */
  bool is_async = false;
  /* M5.5a generator 函数：generator 标记（VM 调用时创建 Generator 对象 +
   * yield 挂起恢复 + next/throw/return 三种恢复路径）。 */
  bool is_generator = false;
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
  /* M5.1e-g 隐式全局赋值：栈顶值 -> 全局对象属性（未声明标识符，非严格）。 */
  void store_global(const std::string &name) {
    push_op(Opcode::StoreGlobal, add_constant(name));
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
  /* M5.1e-h 成员展开调用：obj, func, args_array -> 结果（this=obj）。 */
  void call_method_array() { push_op(Opcode::CallMethodArray); }
  void copy_object_props(uint16_t slot) { push_op(Opcode::CopyObjectProps, slot); }
  void ret() { push_op(Opcode::Ret); }
  void print() { push_op(Opcode::Print); }
  /* M5.4：await x（栈顶值 PromiseResolve 接合，未决则挂起）。 */
  void await() { push_op(Opcode::Await); }
  /* M5.5a：yield <expr>（栈顶 = yield 值；挂起当前 generator 帧，
   * 把值返回给 next() 调用方；恢复时栈顶替换为 next/throw 传入值）。 */
  void yield() { push_op(Opcode::Yield); }
  /* M5.5a-4：yield* <expr>（栈顶 = inner iterable；VM 驱动 inner 迭代器，
   * 逐个把 value 经本 generator 转发给外层 next()；inner done 后压 inner
   * 返回值继续外层）。 */
  void yield_delegate() { push_op(Opcode::YieldDelegate); }

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
  /* M5.1d-c1：super() 构造调用（argc 个参数，基类构造器于当前帧 this）。 */
  void super_call(uint16_t argc) { push_op(Opcode::SuperCall, argc); }
  /* M5.1d-d：super(...args) 展开调用（栈顶 args 数组，基类构造器于当前帧 this）。 */
  void super_call_array() { push_op(Opcode::SuperCallArray); }
  /* M5.1d-c2：私有字段读写/存在性。private_names 下标 -> 字段全名。 */
  void load_private(uint16_t name_index) { push_op(Opcode::LoadPrivate, name_index); }
  void store_private(uint16_t name_index) { push_op(Opcode::StorePrivate, name_index); }
  void has_private(uint16_t name_index) { push_op(Opcode::HasPrivate, name_index); }
  void init_private(uint16_t name_index) { push_op(Opcode::InitPrivate, name_index); }

  /* M5.1e 运算符补全：in/instanceof/一元转换/delete。 */
  void in() { push_op(Opcode::In); }
  void instance_of() { push_op(Opcode::Instanceof); }
  void to_string() { push_op(Opcode::ToString); }
  void to_number() { push_op(Opcode::ToNumber); }
  void bit_not() { push_op(Opcode::BitNot); }
  void delete_prop(const std::string &key) {
    push_op(Opcode::DeleteProp, add_constant(key));
  }
  void delete_elem() { push_op(Opcode::DeleteElem); }
  /* M5.1e-b：++/--（arg=0 增 1，arg=1 减 1）。 */
  void increment(uint16_t dec = 0) { push_op(Opcode::Increment, dec); }
  /* M5.1e-c for-in：枚举键数组 / 取下一个键。 */
  void enumerate_keys() { push_op(Opcode::EnumerateKeys); }
  void next_key() { push_op(Opcode::NextKey); }
  /* M5.1e-d 对象 rest：复制源中排除键之外的可枚举属性到槽内对象。 */
  void rest_object_props(uint16_t target_slot) {
    push_op(Opcode::RestObjectProps, target_slot);
  }
  /* M5.1e-f 异常：抛栈顶值 / finally 末尾重抛挂起异常。 */
  void throw_value() { push_op(Opcode::Throw); }
  void rethrow_pending() { push_op(Opcode::RethrowPending); }

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

  /* 模块导入（M5.2a）：注册导入绑定（去重，同 local 名唯一），返回下标。 */
  uint16_t add_import(ModuleImportEntry entry) {
    for (size_t i = 0; i < fn_.imports.size(); ++i) {
      if (fn_.imports[i].local_name == entry.local_name) {
        return static_cast<uint16_t>(i);
      }
    }
    fn_.imports.push_back(std::move(entry));
    return static_cast<uint16_t>(fn_.imports.size() - 1);
  }
  void load_import(uint16_t idx) { push_op(Opcode::LoadImport, idx); }
  void store_import(uint16_t idx) { push_op(Opcode::StoreImport, idx); }
  void load_import_meta() { push_op(Opcode::LoadImportMeta); } /* M5.2c import.meta */
  void load_import_namespace(uint16_t idx) { /* M5.2d import * as ns */
    push_op(Opcode::LoadImportNamespace, idx);
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
  /* M5.1e-f：当前指令位置（下一条待发射指令的下标）。try 异常表区间用。 */
  size_t position() const { return fn_.code.size(); }

  /* M5.1e-f：注册 try 异常保护表条目（编译期由 try/catch/finally 生成）。 */
  void add_try_entry(TryEntry e) { fn_.try_entries.push_back(std::move(e)); }

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
