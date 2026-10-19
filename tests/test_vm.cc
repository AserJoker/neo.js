/*
 * M2-VM 字节码解释器单元测试。
 * 验证（用 BytecodeBuilder::push_op 发射式构造字节码，端到端运行）：
 *   1. 常量压栈 + 算术（Add/Sub/Mul/Div/Neg）
 *   2. 字符串拼接 + 比较（==/===/</）
 *   3. 局部变量（LoadVar/StoreVar）
 *   4. 跳转控制流（Jump/JumpIfFalse + label 回填）
 *   5. 函数调用（Call/Ret，宿主回调）
 *   6. print 输出
 *   7. GC 下 VM 栈存活（栈帧入根集）
 */

#include <gtest/gtest.h>

#include <cstring>

#include "neojs/compiler/bytecode.h"
#include "neojs/runtime/context.h"
#include "neojs/runtime/vm.h"

#include "neojs/neojs.h"

namespace {

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
JSContext *makeCtx(JSAllocator **out_alloc) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  EXPECT_NE(a, nullptr);
  JSContext *ctx = JS_NewContext(a);
  EXPECT_NE(ctx, nullptr);
  if (out_alloc) *out_alloc = a;
  return ctx;
}

/* 内部 ctx 便捷访问。 */
neojs::JSContext *ic(JSContext *ctx) {
  return reinterpret_cast<neojs::JSContext *>(ctx);
}

/* 求和回调（宿主函数，供 Call 指令测试）。 */
JSValue *sumFn(JSContext *ctx, JSValue *self, size_t argc, JSValue *argv) {
  (void)self;
  JSValue *const *args = reinterpret_cast<JSValue *const *>(argv);
  double total = 0.0;
  for (size_t i = 0; i < argc; ++i) {
    total += JS_ToNumber(ctx, args[i]);
  }
  return JS_CreateNumber(ctx, total, nullptr);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/* 1. 常量 + 算术                                                              */
/* -------------------------------------------------------------------------- */

/* push 1; push 2; add; ret -> 3 */
TEST(VM, AddNumbers) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b("test");
  b.push_int(1);
  b.push_int(2);
  b.push_op(neojs::Opcode::Add);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* push 7; push 2; sub; push 3; mul; neg -> -(7-2)*3 = -15 */
TEST(VM, ArithChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_int(7);
  b.push_int(2);
  b.push_op(neojs::Opcode::Sub);
  b.push_int(3);
  b.push_op(neojs::Opcode::Mul);
  b.push_op(neojs::Opcode::Neg);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), -15.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* push "foo"; push "bar"; add -> "foobar"（字符串拼接） */
TEST(VM, StringConcat) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_const("foo");
  b.push_const("bar");
  b.push_op(neojs::Opcode::Add);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "foobar");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* push 1; push "2"; add -> "12"（数字 + 字符串拼接） */
TEST(VM, NumberPlusStringConcat) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_int(1);
  b.push_const("2");
  b.push_op(neojs::Opcode::Add);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "12");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 2. 比较                                                                     */
/* -------------------------------------------------------------------------- */

/* push 1; push 1; strict_eq -> true；push 1; push "1"; strict_eq -> false */
TEST(VM, CompareStrictEq) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_int(1);
  b.push_int(1);
  b.push_op(neojs::Opcode::StrictEq);
  b.ret();
  neojs::FunctionBytecode fn = b.build();
  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Boolean);
  EXPECT_TRUE(r.value->toBoolean());

  neojs::BytecodeBuilder b2;
  b2.push_int(1);
  b2.push_const("1");
  b2.push_op(neojs::Opcode::StrictEq);
  b2.ret();
  neojs::FunctionBytecode fn2 = b2.build();
  r = neojs::runBytecode(ic(ctx), fn2, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_FALSE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* push 2; push "10"; lt -> true（字符串转数字） */
TEST(VM, CompareLt) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_int(2);
  b.push_const("10");
  b.push_op(neojs::Opcode::Lt);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(r.value->toBoolean());

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 3. 局部变量                                                                 */
/* -------------------------------------------------------------------------- */

/* numLocals=1；x=5；LoadVar x; LoadVar x; add -> 10 */
TEST(VM, LocalVars) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.set_num_locals(1);
  b.push_int(5);
  b.store_var(0);
  b.load_var(0);
  b.load_var(0);
  b.push_op(neojs::Opcode::Add);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 10.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 入口参数写入局部槽：numLocals=2，args=[3,4]，add -> 7 */
TEST(VM, EntryArgsAsLocals) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.set_num_locals(2);
  b.load_var(0);
  b.load_var(1);
  b.push_op(neojs::Opcode::Add);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::JSValue *x = ic(ctx)->createValue(3.0);
  neojs::JSValue *y = ic(ctx)->createValue(4.0);
  neojs::JSValue *args[] = {x, y};
  neojs::ConversionResult r =
      neojs::runBytecode(ic(ctx), fn, nullptr, args, 2);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 4. 跳转控制流（label 回填）                                                */
/* -------------------------------------------------------------------------- */

/* if (true) {1} else {2}：JumpIfFalse 跳过 then -> 1 */
TEST(VM, JumpIfFalse) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  neojs::BcLabel else_label = b.new_label();
  neojs::BcLabel end_label = b.new_label();

  b.push_int(1);            /* 条件 true */
  b.jump_if_false(&else_label);
  b.push_int(1);            /* then 分支 */
  b.jump(&end_label);
  b.bind_label(else_label);
  b.push_int(2);            /* else 分支 */
  b.bind_label(end_label);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 条件为 false 走 else -> 2 */
TEST(VM, JumpIfFalseElse) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  neojs::BcLabel else_label = b.new_label();
  neojs::BcLabel end_label = b.new_label();

  b.push_int(0);            /* 条件 false */
  b.jump_if_false(&else_label);
  b.push_int(1);
  b.jump(&end_label);
  b.bind_label(else_label);
  b.push_int(2);
  b.bind_label(end_label);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 2.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 循环计数：numLocals=1; i=0; loop: i++; i<3 jump_if_false end; jump loop; end: i
 * 等价 C：int i=0; while(i<3) i++; -> i=3 */
TEST(VM, LoopCounter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.set_num_locals(1);
  neojs::BcLabel loop = b.new_label();
  neojs::BcLabel end = b.new_label();

  b.push_int(0);
  b.store_var(0);          /* i = 0 */
  b.bind_label(loop);
  b.load_var(0);
  b.push_int(1);
  b.push_op(neojs::Opcode::Add);
  b.store_var(0);          /* i = i + 1 */
  b.load_var(0);
  b.push_int(3);
  b.push_op(neojs::Opcode::Lt);
  b.jump_if_false(&end);   /* i < 3 ? continue : end */
  b.jump(&loop);
  b.bind_label(end);
  b.load_var(0);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok()) << "error: " << r.error;
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 5. 函数调用（Call/Ret，宿主回调）                                          */
/* -------------------------------------------------------------------------- */

/* 宿主函数经全局属性注册，字节码 load_global("sumFn") 加载后 call 2 -> 3。
 * 覆盖：LoadGlobal 全局属性读取 + Call 指令调用宿主回调。 */
TEST(VM, CallHostFunction) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 创建宿主函数并注册为全局属性（JS_SetProperty 返回 value 自身句柄，
   * 不再对返回值 FreeValue——句柄槽复用会破坏引用，见 E5 教训）。 */
  JSValue *fn = JS_NewCFunction(ctx, sumFn, nullptr);
  ASSERT_NE(fn, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_SetProperty(ctx, global, "sumFn", fn);

  /* 字节码：load_global("sumFn"); push 1; push 2; call 2 -> 3 */
  neojs::BytecodeBuilder b;
  b.load_global("sumFn");
  b.push_int(1);
  b.push_int(2);
  b.call(2);
  b.ret();
  neojs::FunctionBytecode fbc = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fbc, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeValue(ctx, global);
  JS_FreeValue(ctx, fn);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* LoadGlobal 读取未定义全局属性 -> undefined（不抛错）。 */
TEST(VM, LoadGlobalMissingProperty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.load_global("no_such_prop");
  b.ret();
  neojs::FunctionBytecode fbc = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fbc, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Undefined);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 6. print 输出                                                              */
/* -------------------------------------------------------------------------- */

/* print 弹出栈顶输出。用 capture 验证（v1：重定向 stdout 不可靠，
 * 仅验证执行不崩 + 结果正确）。 */
TEST(VM, PrintTop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_int(42);
  b.print();
  b.push_int(7);
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 7.0); /* print 后栈顶应为 7 */

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 7. GC 下 VM 栈存活（栈帧入根集）                                           */
/* -------------------------------------------------------------------------- */

/* 大量分配触发 GC 后，VM 栈上的中间值仍存活（栈帧根集）。
 * push "kept"（字符串对象）；分配大量临时值触发 GC；ret 返回该字符串。 */
TEST(VM, StackSurvivesGc) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.push_const("kept");
  /* 循环分配 2000 个临时值触发自动 GC（默认阈值 1000）；
   * 每压一个临时值立即 pop 清理，仅栈底的 "kept" 保持存活。 */
  for (int i = 0; i < 2000; ++i) {
    b.push_int(i);
    b.pop();
  }
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "kept");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 8. C API 字节码执行入口（JS_RunBytecode 端到端）                           */
/* -------------------------------------------------------------------------- */

/* JS_BC_* 枚举与内部 Opcode 数值对齐（neojs.h 注释保证），扁平数组直接映射。 */
TEST(VM, CapiRunBytecodeAdd) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* push 1; push 2; add; ret -> 3 */
  const JSBCOp ops[] = {JS_BC_PUSH_INT, JS_BC_PUSH_INT, JS_BC_ADD, JS_BC_RET};
  const uint16_t args[] = {1, 2, 0, 0};
  JSValue *r = JS_RunBytecode(ctx, ops, 4, args, nullptr, 0, 0, 0, nullptr);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 局部变量循环计数：i=0; while(i<3) i++ -> 3（C API 构造，含跳转）。 */
TEST(VM, CapiRunBytecodeLoop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 指令序列（绝对跳转位置，无 label）：
   *   [0] push_int 0        i = 0
   *   [1] store_var 0
   *   [2] load_var 0        loop:
   *   [3] push_int 1
   *   [4] add               i = i + 1
   *   [5] store_var 0
   *   [6] load_var 0
   *   [7] push_int 3
   *   [8] lt
   *   [9] jump_if_false 12   i < 3 ? loop : end
   *   [10] jump 2
   *   [11] nop
   *   [12] load_var 0       end:
   *   [13] ret */
  const JSBCOp ops[] = {JS_BC_PUSH_INT, JS_BC_STORE_VAR, JS_BC_LOAD_VAR,
                        JS_BC_PUSH_INT, JS_BC_ADD,       JS_BC_STORE_VAR,
                        JS_BC_LOAD_VAR, JS_BC_PUSH_INT,  JS_BC_LT,
                        JS_BC_JUMP_IF_FALSE, JS_BC_JUMP, JS_BC_NOP,
                        JS_BC_LOAD_VAR, JS_BC_RET};
  const uint16_t args[] = {0, 0, 0, 1, 0, 0, 0, 3, 0, 12, 2, 0, 0, 0};
  JSValue *r = JS_RunBytecode(ctx, ops, 14, args, nullptr, 0, 1, 0, nullptr);
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 常量池 + 字符串拼接：push "foo"; push "bar"; add; ret -> "foobar"。 */
TEST(VM, CapiRunBytecodeString) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  const JSBCOp ops[] = {JS_BC_PUSH_CONST, JS_BC_PUSH_CONST, JS_BC_ADD,
                        JS_BC_RET};
  const uint16_t args[] = {0, 1, 0, 0};
  const char *const constants[] = {"foo", "bar"};
  JSValue *r =
      JS_RunBytecode(ctx, ops, 4, args, constants, 2, 0, 0, nullptr);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING);
  EXPECT_STREQ(JS_ToString(ctx, r), "foobar");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 入口参数：numLocals=2, args=[3,4], load_var 0 + load_var 1 + add -> 7。 */
TEST(VM, CapiRunBytecodeArgs) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *x = JS_CreateNumber(ctx, 3.0, nullptr);
  JSValue *y = JS_CreateNumber(ctx, 4.0, nullptr);
  JSValue *argv[] = {x, y};

  const JSBCOp ops[] = {JS_BC_LOAD_VAR, JS_BC_LOAD_VAR, JS_BC_ADD, JS_BC_RET};
  const uint16_t args[] = {0, 1, 0, 0};
  JSValue *r = JS_RunBytecode(ctx, ops, 4, args, nullptr, 0, 2, 2, argv);
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);
  JS_FreeValue(ctx, x);
  JS_FreeValue(ctx, y);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
