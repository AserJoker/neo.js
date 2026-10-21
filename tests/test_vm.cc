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

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

/* -------------------------------------------------------------------------- */
/* 9. Compiler：AST → 字节码 → VM 端到端（M2.4 ① 表达式编译）                */
/* -------------------------------------------------------------------------- */

#include "neojs/compiler/compiler.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"

namespace {

/* 解析 + 编译 + 运行源码，返回执行结果。失败时返回空 ConversionResult。 */
neojs::ConversionResult compileAndRun(neojs::JSContext *c, const char *src) {
  neojs::DiagnosticSink sink;
  neojs::AST *ast = neojs::parseSource(&c->allocator(), src, std::strlen(src), sink);
  if (ast == nullptr) {
    return {}; /* 语法错误 */
  }
  neojs::Compiler compiler;
  neojs::CompileResult cr = compiler.compile(*ast);
  c->allocator().destroy(ast);
  if (!cr.ok) {
    return {}; /* 编译错误 */
  }
  return neojs::runBytecode(c, cr.fn, nullptr, nullptr, 0);
}

}  // namespace

/* 1 + 2 -> 3 */
TEST(Compiler, AddExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "1 + 2");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* (1 + 2) * 3 - 4 / 2 -> 7 */
TEST(Compiler, ArithPrecedence) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "(1 + 2) * 3 - 4 / 2");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 5 % 2 + 2 ** 3 -> 1 + 8 = 9 */
TEST(Compiler, ModPow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "5 % 2 + 2 ** 3");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 9.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 字符串拼接："foo" + "bar" -> "foobar" */
TEST(Compiler, StringConcatExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "'foo' + 'bar'");
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::String);
  EXPECT_EQ(r.value->toString(), "foobar");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 比较：1 < 2 -> true；5 > 6 -> false；3 <= 3 -> true；4 >= 4 -> true */
TEST(Compiler, RelationalExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "1 < 2");
  ASSERT_TRUE(r1.ok());
  EXPECT_EQ(r1.value->toBoolean(), true);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "5 > 6");
  ASSERT_TRUE(r2.ok());
  EXPECT_EQ(r2.value->toBoolean(), false);

  neojs::ConversionResult r3 = compileAndRun(ic(ctx), "3 <= 3");
  ASSERT_TRUE(r3.ok());
  EXPECT_EQ(r3.value->toBoolean(), true);

  neojs::ConversionResult r4 = compileAndRun(ic(ctx), "4 >= 4");
  ASSERT_TRUE(r4.ok());
  EXPECT_EQ(r4.value->toBoolean(), true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 相等：1 == 1 -> true；1 === '1' -> false；1 != 2 -> true；1 !== '1' -> true */
TEST(Compiler, EqualityExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "1 == 1");
  ASSERT_TRUE(r1.ok());
  EXPECT_EQ(r1.value->toBoolean(), true);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "1 === '1'");
  ASSERT_TRUE(r2.ok());
  EXPECT_EQ(r2.value->toBoolean(), false);

  neojs::ConversionResult r3 = compileAndRun(ic(ctx), "1 != 2");
  ASSERT_TRUE(r3.ok());
  EXPECT_EQ(r3.value->toBoolean(), true);

  neojs::ConversionResult r4 = compileAndRun(ic(ctx), "1 !== '1'");
  ASSERT_TRUE(r4.ok());
  EXPECT_EQ(r4.value->toBoolean(), true);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 位运算：5 & 3 -> 1；5 | 3 -> 7；5 ^ 3 -> 6；1 << 4 -> 16 */
TEST(Compiler, BitwiseExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "5 & 3");
  ASSERT_TRUE(r1.ok());
  EXPECT_DOUBLE_EQ(r1.value->toNumber(), 1.0);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "5 | 3");
  ASSERT_TRUE(r2.ok());
  EXPECT_DOUBLE_EQ(r2.value->toNumber(), 7.0);

  neojs::ConversionResult r3 = compileAndRun(ic(ctx), "5 ^ 3");
  ASSERT_TRUE(r3.ok());
  EXPECT_DOUBLE_EQ(r3.value->toNumber(), 6.0);

  neojs::ConversionResult r4 = compileAndRun(ic(ctx), "1 << 4");
  ASSERT_TRUE(r4.ok());
  EXPECT_DOUBLE_EQ(r4.value->toNumber(), 16.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 一元：-5 -> -5；!0 -> true；typeof 'x' -> "string" */
TEST(Compiler, UnaryExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "-5");
  ASSERT_TRUE(r1.ok());
  EXPECT_DOUBLE_EQ(r1.value->toNumber(), -5.0);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "!0");
  ASSERT_TRUE(r2.ok());
  EXPECT_EQ(r2.value->toBoolean(), true);

  neojs::ConversionResult r3 = compileAndRun(ic(ctx), "typeof 'x'");
  ASSERT_TRUE(r3.ok());
  EXPECT_EQ(r3.value->toString(), "string");

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 逻辑短路：0 && 5 -> 0；3 && 4 -> 4；0 || 5 -> 5；3 || 4 -> 3 */
TEST(Compiler, LogicalShortCircuit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "0 && 5");
  ASSERT_TRUE(r1.ok());
  EXPECT_DOUBLE_EQ(r1.value->toNumber(), 0.0);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "3 && 4");
  ASSERT_TRUE(r2.ok());
  EXPECT_DOUBLE_EQ(r2.value->toNumber(), 4.0);

  neojs::ConversionResult r3 = compileAndRun(ic(ctx), "0 || 5");
  ASSERT_TRUE(r3.ok());
  EXPECT_DOUBLE_EQ(r3.value->toNumber(), 5.0);

  neojs::ConversionResult r4 = compileAndRun(ic(ctx), "3 || 4");
  ASSERT_TRUE(r4.ok());
  EXPECT_DOUBLE_EQ(r4.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 三元：1 ? 10 : 20 -> 10；0 ? 10 : 20 -> 20 */
TEST(Compiler, ConditionalExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "1 ? 10 : 20");
  ASSERT_TRUE(r1.ok());
  EXPECT_DOUBLE_EQ(r1.value->toNumber(), 10.0);

  neojs::ConversionResult r2 = compileAndRun(ic(ctx), "0 ? 10 : 20");
  ASSERT_TRUE(r2.ok());
  EXPECT_DOUBLE_EQ(r2.value->toNumber(), 20.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 变量声明 + 赋值：var a = 1; a = a + 2; a -> 3 */
TEST(Compiler, VarAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "var a = 1; a = a + 2; a");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 复合赋值：var a = 10; a += 5; a -> 15 */
TEST(Compiler, CompoundAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "var a = 10; a += 5; a");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 15.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 逗号序列：1, 2, 3 -> 3 */
TEST(Compiler, SequenceExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "1, 2, 3");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 解构声明编译执行（M5.1a）：var [a] = [1]; a */
TEST(Compiler, DestructuringDeclaration) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(ic(ctx), "var [a] = [1]; a");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 10. Compiler 语句编译（M2.4 ②）                                           */
/* -------------------------------------------------------------------------- */

/* if-else：if (1) 10; else 20; —— 顶层 completion value 为最后表达式值 */
TEST(Compiler, IfElse) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r1 = compileAndRun(ic(ctx), "if (1) 10; else 20; 99");
  ASSERT_TRUE(r1.ok());
  EXPECT_DOUBLE_EQ(r1.value->toNumber(), 99.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* if 无 else + 变量赋值 */
TEST(Compiler, IfAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r =
      compileAndRun(ic(ctx), "var a = 0; if (1) { a = 5; } a");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 5.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* while 循环：var i = 0; while (i < 3) { i = i + 1; } i -> 3 */
TEST(Compiler, WhileLoop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r =
      compileAndRun(ic(ctx), "var i = 0; while (i < 3) { i = i + 1; } i");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* while 累加：sum 1..10 -> 55 */
TEST(Compiler, WhileSum) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(
      ic(ctx), "var i = 1; var sum = 0; while (i <= 10) { sum = sum + i; i = i + 1; } sum");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 55.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* do-while：var i = 0; do { i = i + 1; } while (i < 3); i -> 3 */
TEST(Compiler, DoWhileLoop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r =
      compileAndRun(ic(ctx), "var i = 0; do { i = i + 1; } while (i < 3); i");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* for 循环：for (var i = 0; i < 3; i = i + 1) {} i -> 3 */
TEST(Compiler, ForLoop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(
      ic(ctx), "var i = 0; for (var j = 0; j < 3; j = j + 1) { i = i + 1; } i");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 嵌套循环：乘法表内层计数 */
TEST(Compiler, NestedLoop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r = compileAndRun(
      ic(ctx),
      "var n = 0; for (var i = 0; i < 3; i = i + 1) { "
      "for (var j = 0; j < 3; j = j + 1) { n = n + 1; } } n");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 9.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* return 语句（顶层 return 是语法错误，故放块内也报错——
 * 这里仅验证 return 编译：函数编译在③实现，顶层 return 不出现）。
 * 用一个无函数 return 的脚本验证正常路径不受影响。 */
TEST(Compiler, BlockScope) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::ConversionResult r =
      compileAndRun(ic(ctx), "var a = 1; { var b = 2; a = a + b; } a");
  ASSERT_TRUE(r.ok());
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 3.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 11. JS_Eval 端到端（M2.4 ③：源码 → 字节码 → VM）                          */
/* -------------------------------------------------------------------------- */

/* JS_Eval 求值：1 + 2 -> 3 */
TEST(Compiler, EvalAdd) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "1 + 2", std::strlen("1 + 2"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 循环：while 累加 1..10 -> 55 */
TEST(Compiler, EvalWhileSum) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "var i = 1; var sum = 0; while (i <= 10) { sum = sum + i; i = i + 1; } sum",
      std::strlen("var i = 1; var sum = 0; while (i <= 10) { sum = sum + i; i = i + 1; } sum"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 55.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 函数声明 + 调用：function add(a, b) { return a + b; } add(2, 3) -> 5 */
TEST(Compiler, EvalFunctionCall) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "function add(a, b) { return a + b; } add(2, 3)",
      std::strlen("function add(a, b) { return a + b; } add(2, 3)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 递归：阶乘 fact(5) -> 120 */
TEST(Compiler, EvalRecursion) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } fact(5)",
      std::strlen("function fact(n) { if (n <= 1) { return 1; } return n * fact(n - 1); } fact(5)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 120.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 递归：斐波那契 fib(10) -> 55 */
TEST(Compiler, EvalFibonacci) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); } fib(10)",
      std::strlen("function fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); } fib(10)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 55.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 字符串拼接 + 比较混合 */
TEST(Compiler, EvalMixed) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var s = 'a'; if (1 < 2) { s = s + 'b'; } s",
                       std::strlen("var s = 'a'; if (1 < 2) { s = s + 'b'; } s"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ab");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_Eval 语法错误 -> Exception */
TEST(Compiler, EvalSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var = 3", std::strlen("var = 3"));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 12. 闭包（M2.5）：捕获外层变量 + 环境链                                      */
/* -------------------------------------------------------------------------- */

/* 闭包计数器：每次调用捕获的 count 递增 -> 1, 2, 3 */
TEST(Compiler, EvalClosureCounter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function counter() { var count = 0; return function() { count = count + 1; return count; }; }"
      " var c = counter(); var a1 = c(); var a2 = c(); var a3 = c(); a1 * 100 + a2 * 10 + a3",
      std::strlen("function counter() { var count = 0; return function() { count = count + 1; return count; }; }"
                  " var c = counter(); var a1 = c(); var a2 = c(); var a3 = c(); a1 * 100 + a2 * 10 + a3"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 123.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 闭包捕获参数：makeAdder(x) 返回加 x 的函数 */
TEST(Compiler, EvalClosureCaptureParam) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function makeAdder(x) { return function(y) { return x + y; }; }"
      " var add5 = makeAdder(5); add5(3) + add5(7)",
      std::strlen("function makeAdder(x) { return function(y) { return x + y; }; }"
                  " var add5 = makeAdder(5); add5(3) + add5(7)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 20.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多层闭包（两跳环境链）：counter() 内再包一层 */
TEST(Compiler, EvalClosureNested) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function outer() { var x = 10; return function() { var y = 2; return function() { x = x + y; return x; }; }; }"
      " var f = outer()(); var a1 = f(); var a2 = f(); a1 * 10 + a2",
      std::strlen("function outer() { var x = 10; return function() { var y = 2; return function() { x = x + y; return x; }; }; }"
                  " var f = outer()(); var a1 = f(); var a2 = f(); a1 * 10 + a2"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 134.0); /* a1=12, a2=14 -> 12*10+14 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 递归过深 -> RangeError（M2.5 栈深度上限防爆栈）。
 * 用小上限（64）验证机制：loop 自递归 64 层即触发，远低于 C++ 物理栈风险。 */
TEST(Compiler, EvalStackOverflow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = ic(ctx);
  c->setMaxCallDepth(64);

  JSValue *r = JS_Eval(
      ctx,
      "function loop() { return loop(); } loop()",
      std::strlen("function loop() { return loop(); } loop()"));
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION);
  EXPECT_NE(std::string(JS_ToString(ctx, r)).find("RangeError"),
            std::string::npos)
      << "error: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 13. M2 总验收（M2.6）：复杂复合语法用例                                      */
/* -------------------------------------------------------------------------- */

/* 闭包 + while 循环：闭包内循环累加捕获变量，多次调用间状态持久。 */
TEST(Compiler, EvalClosureLoopCounter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function makeCounter() { var count = 0; return function(n) { while (n > 0) { count = count + 1; n = n - 1; } return count; }; }"
      " var c = makeCounter(); c(3); c(4); c(2)",
      std::strlen("function makeCounter() { var count = 0; return function(n) { while (n > 0) { count = count + 1; n = n - 1; } return count; }; }"
                  " var c = makeCounter(); c(3); c(4); c(2)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0); /* 3+4+2 次累加 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 递归 + 复合赋值 + 三元：尾递归累加求和 1+2+...+10。 */
TEST(Compiler, EvalRecursiveTernarySum) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function sum(n, acc) { if (n == 0) { return acc; } return sum(n - 1, acc + n); } sum(10, 0)",
      std::strlen("function sum(n, acc) { if (n == 0) { return acc; } return sum(n - 1, acc + n); } sum(10, 0)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 55.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 嵌套循环 + 复合赋值：for × while 求三角数（1 + (1+2) + ... + (1+..+10)）。 */
TEST(Compiler, EvalNestedLoopAccumulate) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "var total = 0; for (var i = 1; i <= 10; i = i + 1) { var inner = 0; var j = 1; while (j <= i) { inner = inner + j; j = j + 1; } total = total + inner; } total",
      std::strlen("var total = 0; for (var i = 1; i <= 10; i = i + 1) { var inner = 0; var j = 1; while (j <= i) { inner = inner + j; j = j + 1; } total = total + inner; } total"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 220.0); /* 三角数前 10 项和 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多闭包 + 参数捕获 + 函数组合：makeAdd/makeMul 组合 f(x) = (x+5)*2。 */
TEST(Compiler, EvalClosureCompose) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function makeAdd(a) { return function(x) { return x + a; }; }"
      " function makeMul(m) { return function(x) { return x * m; }; }"
      " var add5 = makeAdd(5); var mul2 = makeMul(2); mul2(add5(10))",
      std::strlen("function makeAdd(a) { return function(x) { return x + a; }; }"
                  " function makeMul(m) { return function(x) { return x * m; }; }"
                  " var add5 = makeAdd(5); var mul2 = makeMul(2); mul2(add5(10))"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 30.0); /* (10+5)*2 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 闭包状态机 + 三元 + 逻辑短路：累加器在正负间切换。 */
TEST(Compiler, EvalClosureStateMachine) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function makeAcc() { var total = 0; var positive = true; return function(x) { if (positive && x > 0) { total = total + x; } else if (x < 0) { total = total - x; } return total; }; }"
      " var acc = makeAcc(); acc(3); acc(5); acc(-2); acc(4)",
      std::strlen("function makeAcc() { var total = 0; var positive = true; return function(x) { if (positive && x > 0) { total = total + x; } else if (x < 0) { total = total - x; } return total; }; }"
                  " var acc = makeAcc(); acc(3); acc(5); acc(-2); acc(4)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 14.0); /* 3+5+(-2)减->+2 +4 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 复杂表达式：算术/比较/逻辑/位/复合赋值/序列大杂烩。 */
TEST(Compiler, EvalComplexExpression) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "var x = 2; x += 3; var y = x * 4; var z = (x > 4) ? (y - 1) : (y + 1); (z << 1) + (y & 7) + (z % 5)",
      std::strlen("var x = 2; x += 3; var y = x * 4; var z = (x > 4) ? (y - 1) : (y + 1); (z << 1) + (y & 7) + (z % 5)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  /* x=5, y=20, z=19；z<<1=38, y&7=4, z%5=4 -> 46 */
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 46.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 闭包在 GC 压力下存活（M2.5：环境引用边进 GC 标记）。
 * 低阈值频繁 GC，闭包计数器仍正确。 */
TEST(Compiler, EvalClosureUnderGC) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 调低 GC 阈值：每次少量分配即触发 GC。 */
  neojs::JSContext *c = ic(ctx);
  c->heap().setGcThreshold(4);

  JSValue *r = JS_Eval(
      ctx,
      "function counter() { var count = 0; return function() { count = count + 1; return count; }; }"
      " var c = counter(); var s = 0; s = s + c(); s = s + c(); s = s + c(); s",
      std::strlen("function counter() { var count = 0; return function() { count = count + 1; return count; }; }"
                  " var c = counter(); var s = 0; s = s + c(); s = s + c(); s = s + c(); s"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0); /* 1+2+3 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 13b. M5.1a 解构/默认值/rest/展开编译（端到端）                              */
/* -------------------------------------------------------------------------- */

/* 解构声明 + 默认值：var [a, b = 2] = [1]; a + b -> 3 */
TEST(Compiler, M51aDestructuringDefault) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var [a, b = 2] = [1]; a + b",
                       std::strlen("var [a, b = 2] = [1]; a + b"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组 rest 解构：var [x, ...rest] = [1, 2, 3]; rest[0] + rest[1] -> 5 */
TEST(Compiler, M51aArrayRest) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx,
                       "var [x, ...rest] = [1, 2, 3]; rest[0] + rest[1]",
                       std::strlen("var [x, ...rest] = [1, 2, 3]; rest[0] + rest[1]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象解构：var {p, q} = {p: 1, q: 2}; p + q -> 3 */
TEST(Compiler, M51aObjectDestructuring) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var {p, q} = {p: 1, q: 2}; p + q",
                       std::strlen("var {p, q} = {p: 1, q: 2}; p + q"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象解构重命名：var {p: q} = {p: 7}; q -> 7 */
TEST(Compiler, M51aObjectDestructuringRename) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var {p: q} = {p: 7}; q",
                       std::strlen("var {p: q} = {p: 7}; q"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 解构赋值（非声明）：var a; [a] = [9]; a -> 9 */
TEST(Compiler, M51aDestructuringAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a; [a] = [9]; a",
                       std::strlen("var a; [a] = [9]; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 默认参数：function f(a, b = 2) { return a + b; } f(1) -> 3 */
TEST(Compiler, M51aDefaultParam) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "function f(a, b = 2) { return a + b; } f(1)",
      std::strlen("function f(a, b = 2) { return a + b; } f(1)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 默认参数引用前参：function f(a, b = a * 2) { return b; } f(5) -> 10 */
TEST(Compiler, M51aDefaultParamRefPrev) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "function f(a, b = a * 2) { return b; } f(5)",
                       std::strlen("function f(a, b = a * 2) { return b; } f(5)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 10.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 默认参数传显式 undefined：f(1, undefined) -> 3（undefined 也触发默认值） */
TEST(Compiler, M51aDefaultParamUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx,
                       "function f(a, b = 2) { return a + b; } f(1, undefined)",
                       std::strlen("function f(a, b = 2) { return a + b; } f(1, undefined)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* rest 参数：function f(a, ...r) { return r[0] + r[1]; } f(1, 2, 3) -> 5 */
TEST(Compiler, M51aRestParam) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx,
                       "function f(a, ...r) { return r[0] + r[1]; } f(1, 2, 3)",
                       std::strlen("function f(a, ...r) { return r[0] + r[1]; } f(1, 2, 3)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* rest 参数空收集：function f(...r) { return r.length; } f() -> 0 */
TEST(Compiler, M51aRestParamEmpty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "function f(...r) { return r.length; } f()",
                       std::strlen("function f(...r) { return r.length; } f()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 0.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组展开字面量：var b = [2, 3]; [1, ...b, 4][3] -> 4 */
TEST(Compiler, M51aArraySpread) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var b = [2, 3]; var c = [1, ...b, 4]; c[0] + c[1] + c[2] + c[3]",
                       std::strlen("var b = [2, 3]; var c = [1, ...b, 4]; c[0] + c[1] + c[2] + c[3]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 10.0); /* 1+2+3+4 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 展开调用：function f(a, b, c) { return a + b + c; } var arr = [1, 2, 3]; f(...arr) -> 6 */
TEST(Compiler, M51aCallSpread) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "function f(a, b, c) { return a + b + c; } var arr = [1, 2, 3]; f(...arr)",
      std::strlen("function f(a, b, c) { return a + b + c; } var arr = [1, 2, 3]; f(...arr)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 展开调用 + 混合实参：f(...[1, 2], 3) -> 6 */
TEST(Compiler, M51aCallSpreadMixed) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "function f(a, b, c) { return a + b + c; } f(...[1, 2], 3)",
      std::strlen("function f(a, b, c) { return a + b + c; } f(...[1, 2], 3)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象展开：var a = {x: 1, y: 2}; var b = {...a, z: 3}; b.x + b.y + b.z -> 6 */
TEST(Compiler, M51aObjectSpread) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx,
                       "var a = {x: 1, y: 2}; var b = {...a, z: 3}; b.x + b.y + b.z",
                       std::strlen("var a = {x: 1, y: 2}; var b = {...a, z: 3}; b.x + b.y + b.z"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 解构参数：function f({p}) { return p; } f({p: 42}) -> 42 */
TEST(Compiler, M51aDestructuredParam) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "function f({p}) { return p; } f({p: 42})",
                       std::strlen("function f({p}) { return p; } f({p: 42})"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 42.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 解构参数 + 默认值：function f({p} = {p: 5}) { return p; } f() -> 5 */
TEST(Compiler, M51aDestructuredParamDefault) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "function f({p} = {p: 5}) { return p; } f()",
                       std::strlen("function f({p} = {p: 5}) { return p; } f()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 嵌套解构 + 默认值：var {a: [b, c = 3]} = {a: [1]}; b + c -> 4 */
TEST(Compiler, M51aNestedDestructuringDefault) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var {a: [b, c = 3]} = {a: [1]}; b + c",
                       std::strlen("var {a: [b, c = 3]} = {a: [1]}; b + c"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 4.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象重命名 + 默认值：var {p: q = 1} = {}; q -> 1 */
TEST(Compiler, M51aObjectRenameDefault) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var {p: q = 1} = {}; q",
                       std::strlen("var {p: q = 1} = {}; q"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 解构赋值表达式值 = 源值：var a, b; var r = ({x: a, y: b} = {x: 1, y: 2}); r.x + r.y -> 3 */
TEST(Compiler, M51aDestructuringAssignValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var a, b; var r = ({x: a, y: b} = {x: 1, y: 2}); r.x + r.y",
      std::strlen("var a, b; var r = ({x: a, y: b} = {x: 1, y: 2}); r.x + r.y"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 13c. M5.1b 逻辑赋值（&&= / ||= / ??=，短路语义）                            */
/* -------------------------------------------------------------------------- */

/* &&= truthy 赋值：var a = 1; a &&= 2; a -> 2 */
TEST(Compiler, M51bLogicalAndAssignEval) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 1; a &&= 2; a",
                       std::strlen("var a = 1; a &&= 2; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 2.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* &&= falsy 短路（不赋值）：var a = 0; a &&= 5; a -> 0 */
TEST(Compiler, M51bLogicalAndAssignShort) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 0; a &&= 5; a",
                       std::strlen("var a = 0; a &&= 5; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 0.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* ||= truthy 短路：var a = 7; a ||= 3; a -> 7 */
TEST(Compiler, M51bLogicalOrAssignShort) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 7; a ||= 3; a",
                       std::strlen("var a = 7; a ||= 3; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* ||= falsy 赋值：var a = 0; a ||= 3; a -> 3 */
TEST(Compiler, M51bLogicalOrAssignEval) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 0; a ||= 3; a",
                       std::strlen("var a = 0; a ||= 3; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* ??= nullish 赋值：var a = null; a ??= 4; a -> 4 */
TEST(Compiler, M51bNullishAssignNull) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = null; a ??= 4; a",
                       std::strlen("var a = null; a ??= 4; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 4.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* ??= 非 nullish 短路：var a = 5; a ??= 4; a -> 5 */
TEST(Compiler, M51bNullishAssignShort) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 5; a ??= 4; a",
                       std::strlen("var a = 5; a ??= 4; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* ??= undefined 赋值：var a; a ??= 6; a -> 6 */
TEST(Compiler, M51bNullishAssignUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a; a ??= 6; a",
                       std::strlen("var a; a ??= 6; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 成员逻辑赋值（点成员）：var o = {x: 1}; o.x ||= 9; o.x -> 1 */
TEST(Compiler, M51bMemberLogicalAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {x: 1}; o.x ||= 9; o.x",
                       std::strlen("var o = {x: 1}; o.x ||= 9; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 成员逻辑赋值（点成员，falsy 赋值）：var o = {x: 0}; o.x ||= 9; o.x -> 9 */
TEST(Compiler, M51bMemberLogicalAssignEval) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {x: 0}; o.x ||= 9; o.x",
                       std::strlen("var o = {x: 0}; o.x ||= 9; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算成员逻辑赋值：var o = {a: 1}; var k = 'a'; o[k] ??= 8; o.a -> 1 */
TEST(Compiler, M51bComputedMemberLogicalAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {a: 1}; var k = 'a'; o[k] ??= 8; o.a",
                       std::strlen("var o = {a: 1}; var k = 'a'; o[k] ??= 8; o.a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 表达式值 = truthy 赋值新值：var a = 1; var r = (a &&= 2); r -> 2 */
TEST(Compiler, M51bLogicalAssignValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 1; var r = (a &&= 2); r",
                       std::strlen("var a = 1; var r = (a &&= 2); r"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 2.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 表达式值 = falsy 短路旧值：var a = 0; var r = (a &&= 2); r -> 0 */
TEST(Compiler, M51bLogicalAssignValueEval) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 0; var r = (a &&= 2); r",
                       std::strlen("var a = 0; var r = (a &&= 2); r"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 0.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 复合表达式：var a = 0; a += 2; a &&= 4; a -> 4 */
TEST(Compiler, M51bLogicalAssignChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 0; a += 2; a &&= 4; a",
                       std::strlen("var a = 0; a += 2; a &&= 4; a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 4.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 13d. M5.1c 对象字面量 method/getter/setter + 计算键                        */
/* -------------------------------------------------------------------------- */

/* method：{ m() { return 42; } } 调用 */
TEST(Compiler, M51cMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = { m() { return 42; } }; o.m()",
                       std::strlen("var o = { m() { return 42; } }; o.m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 42.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* getter：{ get x() { return 7; } } 读取 */
TEST(Compiler, M51cGetter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = { get x() { return 7; } }; o.x",
                       std::strlen("var o = { get x() { return 7; } }; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* setter：{ set x(v) { this._v = v; } } 写入 */
TEST(Compiler, M51cSetter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = { _v: 0, set x(v) { this._v = v; } }; o.x = 9; o._v",
                       std::strlen("var o = { _v: 0, set x(v) { this._v = v; } }; o.x = 9; o._v"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* getter+setter 组合：{ get x, set x } 读写往返 */
TEST(Compiler, M51cGetterSetterPair) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = { _v: 0, get x() { return this._v; }, set x(v) { this._v = v; } }; o.x = 5; o.x",
                       std::strlen("var o = { _v: 0, get x() { return this._v; }, set x(v) { this._v = v; } }; o.x = 5; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算键：{ [k]: 3 } */
TEST(Compiler, M51cComputedKey) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var k = 'a'; var o = { [k]: 3 }; o.a",
                       std::strlen("var k = 'a'; var o = { [k]: 3 }; o.a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算键表达式：{ [1 + 2]: 'x' } */
TEST(Compiler, M51cComputedKeyExpr) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = { [1 + 2]: 'x' }; o[3]",
                       std::strlen("var o = { [1 + 2]: 'x' }; o[3]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "x");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* method 计算键：{ [k]() { return 1; } } */
TEST(Compiler, M51cComputedKeyMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var k = 'm'; var o = { [k]() { return 1; } }; o.m()",
                       std::strlen("var k = 'm'; var o = { [k]() { return 1; } }; o.m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* getter 计算键：{ get [k]() { return 2; } } */
TEST(Compiler, M51cComputedKeyGetter) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var k = 'x'; var o = { get [k]() { return 2; } }; o.x",
                       std::strlen("var k = 'x'; var o = { get [k]() { return 2; } }; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 2.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 13e. M5.1d-a class 声明/表达式 + 方法编译执行                              */
/* -------------------------------------------------------------------------- */

/* new C() 无构造器（默认构造器）：实例化 + 实例方法调用 */
TEST(Compiler, M51dClassBasic) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r =
      JS_Eval(ctx, "class C { m() { return 42; } } var c = new C(); c.m()",
              std::strlen("class C { m() { return 42; } } var c = new C(); c.m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 42.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 构造器：this 字段初始化 + 实例方法读取 */
TEST(Compiler, M51dClassConstructor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class C { constructor(x) { this.x = x; } getX() { return this.x; } } "
      "var c = new C(7); c.getX()",
      std::strlen("class C { constructor(x) { this.x = x; } getX() { return this.x; } } "
                  "var c = new C(7); c.getX()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 实例方法沿原型链查找（定义在 prototype 对象上） */
TEST(Compiler, M51dClassPrototypeMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class C { m() { return this.v; } } var c = new C(); c.v = 5; c.m()",
      std::strlen("class C { m() { return this.v; } } var c = new C(); c.v = 5; c.m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* static 方法：挂到构造器对象 */
TEST(Compiler, M51dClassStaticMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { static s() { return 99; } } C.s()",
      std::strlen("class C { static s() { return 99; } } C.s()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 99.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* getter/setter 访问器方法（挂到 prototype） */
TEST(Compiler, M51dClassAccessor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class C { constructor() { this._v = 0; } get v() { return this._v; } "
      "set v(x) { this._v = x; } } var c = new C(); c.v = 8; c.v",
      std::strlen("class C { constructor() { this._v = 0; } get v() { return this._v; } "
                  "set v(x) { this._v = x; } } var c = new C(); c.v = 8; c.v"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 8.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算键方法：class { [k]() {} } */
TEST(Compiler, M51dClassComputedKey) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var k = 'm'; class C { [k]() { return 3; } } new C().m()",
      std::strlen("var k = 'm'; class C { [k]() { return 3; } } new C().m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* extends 原型链：实例方法继承 + 构造器静态继承 */
TEST(Compiler, M51dClassExtends) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { m() { return 10; } static s() { return 20; } } "
      "class B extends A { m() { return 30; } } "
      "var b = new B(); b.m() + B.s()",
      std::strlen("class A { m() { return 10; } static s() { return 20; } } "
                  "class B extends A { m() { return 30; } } "
                  "var b = new B(); b.m() + B.s()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 50.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* extends 未覆写方法：沿原型链调用基类实例方法 */
TEST(Compiler, M51dClassExtendsInherit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { hi() { return 'base'; } } class B extends A {} "
      "new B().hi()",
      std::strlen("class A { hi() { return 'base'; } } class B extends A {} "
                  "new B().hi()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "base");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* class 表达式：var C = class { ... } */
TEST(Compiler, M51dClassExpression) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var C = class { m() { return 'expr'; } }; new C().m()",
      std::strlen("var C = class { m() { return 'expr'; } }; new C().m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "expr");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 边界：static 作为普通方法名（非修饰符） */
TEST(Compiler, M51dClassStaticAsMethodName) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class A { static() { return 1; } } new A().static()",
      std::strlen("class A { static() { return 1; } } new A().static()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 边界：get constructor 是访问器，不是构造器 */
TEST(Compiler, M51dClassGetConstructorAccessor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class B { get constructor() { return 'accessor'; } } new B().constructor",
      std::strlen("class B { get constructor() { return 'accessor'; } } new B().constructor"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "accessor");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.1d-b class super 属性访问：super.x / super[expr] / super.m()            */
/* -------------------------------------------------------------------------- */

/* super.x 读取基类实例方法（继承链上数据属性） */
TEST(Compiler, M51dSuperReadBaseMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { hi() { return 'A-hi'; } } "
      "class B extends A { go() { return super.hi(); } } "
      "new B().go()",
      std::strlen("class A { hi() { return 'A-hi'; } } "
                  "class B extends A { go() { return super.hi(); } } "
                  "new B().go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "A-hi");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super.m() 调用：this 绑定为当前实例（子类 this 传给基类方法） */
TEST(Compiler, M51dSuperCallThisBinding) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { setTag(v) { this.tag = v; } who() { return this.tag; } } "
      "class B extends A { init() { super.setTag('B'); } "
      "go() { return super.who(); } } "
      "var b = new B(); b.init(); b.go()",
      std::strlen("class A { setTag(v) { this.tag = v; } who() { return this.tag; } } "
                  "class B extends A { init() { super.setTag('B'); } "
                  "go() { return super.who(); } } "
                  "var b = new B(); b.init(); b.go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "B");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super.x 读取基类实例数据属性（非方法） */
TEST(Compiler, M51dSuperReadDataProperty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { getVal() { return 100; } } "
      "class B extends A { v() { return super.getVal(); } } "
      "var o = new B(); o.v()",
      std::strlen("class A { getVal() { return 100; } } "
                  "class B extends A { v() { return super.getVal(); } } "
                  "var o = new B(); o.v()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 100.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super[expr] 计算键访问 */
TEST(Compiler, M51dSuperComputedKey) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { foo() { return 'computed'; } } "
      "class B extends A { go() { return super['fo' + 'o'](); } } "
      "new B().go()",
      std::strlen("class A { foo() { return 'computed'; } } "
                  "class B extends A { go() { return super['fo' + 'o'](); } } "
                  "new B().go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "computed");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super 链式访问：super.x.y（HomeObject 原型链上读到的对象再取属性） */
TEST(Compiler, M51dSuperChainedAccess) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { getObj() { return { val: 7 }; } } "
      "class B extends A { go() { return super.getObj().val; } } "
      "new B().go()",
      std::strlen("class A { getObj() { return { val: 7 }; } } "
                  "class B extends A { go() { return super.getObj().val; } } "
                  "new B().go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* getter 访问器属性：super 读取 getter，this 绑定当前实例 */
TEST(Compiler, M51dSuperAccessorThisBinding) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { get tag() { return 'getter-' + this.name; } } "
      "class B extends A { init() { this.name = 'x'; } "
      "go() { return super.tag; } } "
      "var b = new B(); b.init(); b.go()",
      std::strlen("class A { get tag() { return 'getter-' + this.name; } } "
                  "class B extends A { init() { this.name = 'x'; } "
                  "go() { return super.tag; } } "
                  "var b = new B(); b.init(); b.go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "getter-x");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 箭头函数继承 super 上下文（箭头函数内 super 仍指向外层方法的 HomeObject） */
TEST(Compiler, M51dSuperInsideArrow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { hi() { return 'arrow-hi'; } } "
      "class B extends A { go() { var f = () => super.hi(); return f(); } } "
      "new B().go()",
      std::strlen("class A { hi() { return 'arrow-hi'; } } "
                  "class B extends A { go() { var f = () => super.hi(); return f(); } } "
                  "new B().go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "arrow-hi");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多层继承：super.x 从直接基类原型链读取 */
TEST(Compiler, M51dSuperMultiLevelInherit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { tag() { return 'A'; } } "
      "class B extends A { tag() { return 'B-' + super.tag(); } } "
      "class C extends B { tag() { return 'C-' + super.tag(); } } "
      "new C().tag()",
      std::strlen("class A { tag() { return 'A'; } } "
                  "class B extends A { tag() { return 'B-' + super.tag(); } } "
                  "class C extends B { tag() { return 'C-' + super.tag(); } } "
                  "new C().tag()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "C-B-A");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super 在普通函数内为语法错误（super 上下文不继承进普通函数） */
TEST(Compiler, M51dSuperInPlainFunctionSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class A { m() { return function() { return super.x; }; } }",
      std::strlen("class A { m() { return function() { return super.x; }; } }"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected syntax error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super 在顶层为语法错误 */
TEST(Compiler, M51dSuperAtTopLevelSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var x = super.y;",
                       std::strlen("var x = super.y;"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected syntax error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* static 方法内 super.x：构造器 HomeObject = 构造器自身，extends 后
 * ctor.[[Prototype]] = base，super.x 从 base 读取（static 语义） */
TEST(Compiler, M51dSuperInStaticMethod) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { static who() { return 'static-A'; } } "
      "class B extends A { static go() { return super.who(); } } "
      "B.go()",
      std::strlen("class A { static who() { return 'static-A'; } } "
                  "class B extends A { static go() { return super.who(); } } "
                  "B.go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "static-A");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* static 方法内 super 读取基类 static 访问器属性 */
TEST(Compiler, M51dSuperStaticDataProperty) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { static get tag() { return 'static-tag'; } } "
      "class B extends A { static go() { return super.tag; } } "
      "B.go()",
      std::strlen("class A { static get tag() { return 'static-tag'; } } "
                  "class B extends A { static go() { return super.tag; } } "
                  "B.go()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "static-tag");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 构造器内 super.x（static 语义）：构造器 HomeObject = 构造器自身，
 * super.x 从 base 读取（base 有 static 方法） */
TEST(Compiler, M51dSuperInConstructor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { static greet() { return 'ctor-super'; } } "
      "class B extends A { constructor() { this.v = super.greet(); } } "
      "new B().v",
      std::strlen("class A { static greet() { return 'ctor-super'; } } "
                  "class B extends A { constructor() { this.v = super.greet(); } } "
                  "new B().v"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ctor-super");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.1d-c1 super() 构造调用                                                 */
/* -------------------------------------------------------------------------- */

/* 派生构造器 super(x)：基类构造器初始化 this */
TEST(Compiler, M51dSuperCallBasic) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { constructor(x) { this.x = x; } } "
      "class B extends A { constructor() { super(42); } } "
      "new B().x",
      std::strlen("class A { constructor(x) { this.x = x; } } "
                  "class B extends A { constructor() { super(42); } } "
                  "new B().x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 42.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super() 后派生构造器继续用 this（this 就是 super 初始化的实例） */
TEST(Compiler, M51dSuperCallThenThis) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { constructor(x) { this.base = x; } } "
      "class B extends A { constructor(x) { super(x); this.derived = this.base * 2; } } "
      "var b = new B(5); b.derived",
      std::strlen("class A { constructor(x) { this.base = x; } } "
                  "class B extends A { constructor(x) { super(x); this.derived = this.base * 2; } } "
                  "var b = new B(5); b.derived"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 10.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super() 带多参数 + 基类方法在派生类实例上可调用 */
TEST(Compiler, M51dSuperCallMultiArgs) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { constructor(a, b) { this.sum = a + b; } getSum() { return this.sum; } } "
      "class B extends A { constructor() { super(3, 4); } } "
      "new B().getSum()",
      std::strlen("class A { constructor(a, b) { this.sum = a + b; } getSum() { return this.sum; } } "
                  "class B extends A { constructor() { super(3, 4); } } "
                  "new B().getSum()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 多层继承：每层构造器 super() 链式调用 */
TEST(Compiler, M51dSuperCallChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "class A { constructor() { this.chain = 'A'; } } "
      "class B extends A { constructor() { super(); this.chain += 'B'; } } "
      "class C extends B { constructor() { super(); this.chain += 'C'; } } "
      "new C().chain",
      std::strlen("class A { constructor() { this.chain = 'A'; } } "
                  "class B extends A { constructor() { super(); this.chain += 'B'; } } "
                  "class C extends B { constructor() { super(); this.chain += 'C'; } } "
                  "new C().chain"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ABC");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 基类构造器返回对象：super() 用该对象作为 this */
TEST(Compiler, M51dSuperCallReturnObject) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "var other = { tag: 'replaced' }; "
      "class A { constructor() { return other; } } "
      "class B extends A { constructor() { var self = super(); this.v = self.tag; } } "
      "new B().v",
      std::strlen("var other = { tag: 'replaced' }; "
                  "class A { constructor() { return other; } } "
                  "class B extends A { constructor() { var self = super(); this.v = self.tag; } } "
                  "new B().v"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "replaced");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super() 在基类构造器内为语法错误 */
TEST(Compiler, M51dSuperCallInBaseCtorSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class A { constructor() { super(); } }",
      std::strlen("class A { constructor() { super(); } }"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected syntax error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* super() 在普通方法内为语法错误 */
TEST(Compiler, M51dSuperCallInMethodSyntaxError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class B extends A { m() { super(); } }",
      std::strlen("class B extends A { m() { super(); } }"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected syntax error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M5.1d-c2 私有字段 #x（独立私有槽表）                                       */
/* -------------------------------------------------------------------------- */

/* 私有字段基本读写：#x 初始化 + 读 + 写 */
TEST(Compiler, M51dC2PrivateFieldBasic) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 1; get() { return this.#x; } set(v) { this.#x = v; } } "
           "var c = new C(); c.get() + '|' + (c.set(42), c.get())",
      std::strlen("class C { #x = 1; get() { return this.#x; } set(v) { this.#x = v; } } "
                  "var c = new C(); c.get() + '|' + (c.set(42), c.get())"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "1|42");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段无初始化器：默认 undefined */
TEST(Compiler, M51dC2PrivateFieldDefaultUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x; get() { return this.#x; } } "
           "var c = new C(); c.get() === undefined",
      std::strlen("class C { #x; get() { return this.#x; } } "
                  "var c = new C(); c.get() === undefined"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN);
  EXPECT_TRUE(JS_ToBoolean(ctx, r));
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段方法调用：#m(args) */
TEST(Compiler, M51dC2PrivateMethodCall) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 5; #m(n) { return this.#x + n; } get(n) { return this.#m(n); } } "
           "var c = new C(); c.get(3)",
      std::strlen("class C { #x = 5; #m(n) { return this.#x + n; } get(n) { return this.#m(n); } } "
                  "var c = new C(); c.get(3)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 8.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段 #x in obj：接收者对象有该字段 true，否则 false */
TEST(Compiler, M51dC2PrivateFieldIn) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 1; static has(o) { return #x in o; } } "
           "var c = new C(); C.has(c) + '|' + C.has({})",
      std::strlen("class C { #x = 1; static has(o) { return #x in o; } } "
                  "var c = new C(); C.has(c) + '|' + C.has({})"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "true|false");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段复合赋值：#x += 1 */
TEST(Compiler, M51dC2PrivateFieldCompoundAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 1; inc() { this.#x += 10; return this.#x; } } "
           "var c = new C(); c.inc()",
      std::strlen("class C { #x = 1; inc() { this.#x += 10; return this.#x; } } "
                  "var c = new C(); c.inc()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 11.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段继承：子类 super() 后父类在 this 上建私有槽，子类方法访问 */
TEST(Compiler, M51dC2PrivateFieldInheritance) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class A { #a = 10; get() { return this.#a; } } "
           "class B extends A { #b = 20; get2() { return this.#b; } } "
           "var b = new B(); b.get() + '|' + b.get2()",
      std::strlen("class A { #a = 10; get() { return this.#a; } } "
                  "class B extends A { #b = 20; get2() { return this.#b; } } "
                  "var b = new B(); b.get() + '|' + b.get2()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "10|20");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段不可见：obj[\"#x\"] 访问不到（独立槽表） */
TEST(Compiler, M51dC2PrivateFieldInvisible) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 私有字段对 class 内 #x in 可见（true），但作为字符串属性访问不可见
   * （c['#x'] === undefined）：独立槽表不暴露为属性。普通字符串 in
   * 操作符尚未实现（M5.1e），故用 class 内 #x in 验证存在性。 */
  JSValue *r = JS_Eval(
      ctx, "class C { #x = 1; static has(o) { return #x in o; } } "
           "var c = new C(); C.has(c) + '|' + (c['#x'] === undefined)",
      std::strlen("class C { #x = 1; static has(o) { return #x in o; } } "
                  "var c = new C(); C.has(c) + '|' + (c['#x'] === undefined)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "true|true");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段 TypeError：跨类访问接收者无该私有槽 */
TEST(Compiler, M51dC2PrivateFieldTypeError) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* C1 的方法访问 C2 实例：接收者无 C1 的私有槽 -> TypeError */
  JSValue *r = JS_Eval(
      ctx, "class C1 { #x = 1; get(o) { return o.#x; } } "
           "class C2 { } var c1 = new C1(); var c2 = new C2(); c1.get(c2)",
      std::strlen("class C1 { #x = 1; get(o) { return o.#x; } } "
                  "class C2 { } var c1 = new C1(); var c2 = new C2(); c1.get(c2)"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION) << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段未声明引用：编译错误 */
TEST(Compiler, M51dC2PrivateFieldUndeclared) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "class C { m() { return this.#y; } }",
                       std::strlen("class C { m() { return this.#y; } }"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected compile error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段普通函数嵌套不可见：function 内 #x 未声明编译错误 */
TEST(Compiler, M51dC2PrivateFieldNotInFunction) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 1; m() { function f() { return this.#x; } return f.call(this); } } "
           "var c = new C(); c.m()",
      std::strlen("class C { #x = 1; m() { function f() { return this.#x; } return f.call(this); } } "
                  "var c = new C(); c.m()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "expected compile error, got: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 私有字段箭头函数继承：箭头函数可访问外层方法私有字段 */
TEST(Compiler, M51dC2PrivateFieldInArrow) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class C { #x = 7; get() { var f = () => this.#x; return f(); } } "
           "var c = new C(); c.get()",
      std::strlen("class C { #x = 7; get() { var f = () => this.#x; return f(); } } "
                  "var c = new C(); c.get()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 箭头函数词法 this（非 class 场景）：普通对象方法内箭头函数继承 this */
TEST(Compiler, M51dC2ArrowLexicalThis) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var o = { n: 42, get() { var f = () => this.n; return f(); } }; "
           "o.get()",
      std::strlen("var o = { n: 42, get() { var f = () => this.n; return f(); } }; "
                  "o.get()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 42.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 派生类构造器：super() 后初始化私有字段 */
TEST(Compiler, M51dC2PrivateFieldDerivedCtor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "class A { #a = 100; get() { return this.#a; } } "
           "class B extends A { #b = 200; constructor() { super(); this.#b = 300; } get2() { return this.#b; } } "
           "var b = new B(); b.get() + '|' + b.get2()",
      std::strlen("class A { #a = 100; get() { return this.#a; } } "
                  "class B extends A { #b = 200; constructor() { super(); this.#b = 300; } get2() { return this.#b; } } "
                  "var b = new B(); b.get() + '|' + b.get2()"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "100|300");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 14. M3.0 VM 对象指令（NewObject/NewArray/LoadProp/StoreProp/LoadElem/      */
/*     StoreElem）                                                            */
/* -------------------------------------------------------------------------- */

/* NewObject + StoreProp + LoadProp：{a: 1} 读写往返（对象暂存局部槽） */
TEST(VM, ObjectPropRoundtrip) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.new_object();
  b.store_var(0);       /* obj 暂存槽 0 */
  b.load_var(0);        /* obj */
  b.push_int(1);        /* obj, 1 */
  b.store_prop("a");    /* obj.a = 1 -> 1 */
  b.pop();              /* 丢弃赋值结果 */
  b.load_var(0);        /* obj */
  b.load_prop("a");     /* obj.a -> 1 */
  b.ret();
  b.set_num_locals(1);
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok()) << r.error;
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 1.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* NewArray + StoreElem + LoadElem：arr[2] = 7 读写往返（对象暂存局部槽） */
TEST(VM, ArrayElemRoundtrip) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.new_array();
  b.store_var(0);       /* arr 暂存槽 0 */
  b.load_var(0);        /* arr */
  b.push_int(2);        /* arr, 2 */
  b.push_int(7);        /* arr, 2, 7 */
  b.store_elem();       /* arr[2] = 7 -> 7 */
  b.pop();              /* 丢弃赋值结果 */
  b.load_var(0);        /* arr */
  b.push_int(2);        /* arr, 2 */
  b.load_elem();        /* arr[2] -> 7 */
  b.ret();
  b.set_num_locals(1);
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok()) << r.error;
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 7.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* LoadProp 未定义属性 -> undefined（对象无该键） */
TEST(VM, ObjectMissingPropUndefined) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.new_object();
  b.load_prop("missing");
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Undefined);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* StoreProp 赋值表达式值 = 新值（链式 obj.a = obj.b = 5） */
TEST(VM, StorePropValueIsRhs) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::BytecodeBuilder b;
  b.new_object();
  b.new_object();
  b.push_int(5);
  b.store_prop("b");   /* o2.b = 5 -> 5 */
  b.store_prop("a");   /* o1.a = (o2.b=5 的值) -> 5 */
  b.ret();
  neojs::FunctionBytecode fn = b.build();

  neojs::ConversionResult r = neojs::runBytecode(ic(ctx), fn, nullptr, nullptr, 0);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->kind(), neojs::Kind::Number);
  EXPECT_DOUBLE_EQ(r.value->toNumber(), 5.0);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 15. M3.1 对象/数组字面量编译与执行（JS_Eval 端到端）                        */
/* -------------------------------------------------------------------------- */

/* 对象字面量：var o = {a: 1, b: 2}; o.a + o.b -> 3（含 StoreVar/StoreProp 往返） */
TEST(Compiler, EvalObjectLiteral) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {a: 1, b: 2}; o.a + o.b",
                       std::strlen("var o = {a: 1, b: 2}; o.a + o.b"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象字面量嵌套 + 字符串键 + 数字键：{s: 'x', 1: 10} 与 {a: {b: 5}} -> 15 */
TEST(Compiler, EvalObjectNestedKeys) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var o = {s: 'x', 1: 10}; var n = {a: {b: 5}}; var t = o.s; n.a.b + o[1]",
      std::strlen("var o = {s: 'x', 1: 10}; var n = {a: {b: 5}}; var t = o.s; n.a.b + o[1]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 15.0); /* 5 + 10 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组字面量：var a = [1, 2, 3]; a[0] + a[1] + a[2] -> 6 */
TEST(Compiler, EvalArrayLiteral) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = [1, 2, 3]; a[0] + a[1] + a[2]",
                       std::strlen("var a = [1, 2, 3]; a[0] + a[1] + a[2]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组字面量稀疏空洞 + 嵌套数组：[[1, 2], [3]][1][0] -> 3 */
TEST(Compiler, EvalArrayNestedSparse) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = [[1, 2], [3]]; a[1][0]",
                       std::strlen("var a = [[1, 2], [3]]; a[1][0]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象简写属性：var a = 3; {a}.a -> 3 */
TEST(Compiler, EvalObjectShorthand) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = 3; var o = {a}; o.a",
                       std::strlen("var a = 3; var o = {a}; o.a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象字面量作为函数返回值 + 数组元素访问混合 */
TEST(Compiler, EvalObjectInFunction) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx,
      "function make(x) { return {v: x, arr: [x, x + 1]}; }"
      " var m = make(4); m.v + m.arr[1]",
      std::strlen("function make(x) { return {v: x, arr: [x, x + 1]}; }"
                  " var m = make(4); m.v + m.arr[1]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0); /* 4 + 5 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象/数组字面量在 GC 压力下存活（对象引用边进 GC 标记） */
TEST(Compiler, EvalObjectUnderGC) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = ic(ctx);
  c->heap().setGcThreshold(4);

  JSValue *r = JS_Eval(
      ctx, "var o = {a: 1, b: [2, 3]}; var s = 0; s = s + o.a; s = s + o.b[1]; s",
      std::strlen("var o = {a: 1, b: [2, 3]}; var s = 0; s = s + o.a; s = s + o.b[1]; s"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 4.0); /* 1 + 3 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 16. M3.2 成员赋值编译（obj.prop = rhs / obj[expr] = rhs / 复合 op=）       */
/* -------------------------------------------------------------------------- */

/* 对象属性赋值：var o = {a: 1}; o.a = 5; o.a -> 5 */
TEST(Compiler, EvalMemberAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {a: 1}; o.a = 5; o.a",
                       std::strlen("var o = {a: 1}; o.a = 5; o.a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算键赋值：var o = {x: 1}; o['x'] = 7; o.x -> 7 */
TEST(Compiler, EvalElemAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {x: 1}; o['x'] = 7; o.x",
                       std::strlen("var o = {x: 1}; o['x'] = 7; o.x"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 7.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组元素赋值：var a = [1, 2, 3]; a[1] = 9; a[1] -> 9 */
TEST(Compiler, EvalArrayElemAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = [1, 2, 3]; a[1] = 9; a[1]",
                       std::strlen("var a = [1, 2, 3]; a[1] = 9; a[1]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 9.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 属性复合赋值：var o = {a: 1}; o.a += 4; o.a -> 5 */
TEST(Compiler, EvalMemberCompoundAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {a: 1}; o.a += 4; o.a",
                       std::strlen("var o = {a: 1}; o.a += 4; o.a"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 数组元素复合赋值：var a = [1, 2]; a[0] *= 5; a[0] -> 5 */
TEST(Compiler, EvalElemCompoundAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var a = [1, 2]; a[0] *= 5; a[0]",
                       std::strlen("var a = [1, 2]; a[0] *= 5; a[0]"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 计算键复合赋值：var o = {k: 2}; var kk = 'k'; o[kk] += 3; o.k -> 5 */
TEST(Compiler, EvalComputedKeyCompoundAssign) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(
      ctx, "var o = {k: 2}; var kk = 'k'; o[kk] += 3; o.k",
      std::strlen("var o = {k: 2}; var kk = 'k'; o[kk] += 3; o.k"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 5.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 赋值表达式值 = 新值（链式成员赋值）：var o = {a: {b: 1}}; o.a.b = 3 -> 3 */
TEST(Compiler, EvalMemberAssignValue) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *r = JS_Eval(ctx, "var o = {a: {b: 1}}; o.a.b = 3",
                       std::strlen("var o = {a: {b: 1}}; o.a.b = 3"));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* 10. M3.3 原型链：Object.getPrototypeOf / setPrototypeOf / create           */
/* -------------------------------------------------------------------------- */

/* 对象字面量原型 = Object.prototype：Object.getPrototypeOf({}) === Object.prototype。
 * 需先触发 installBuiltins（JS_GetGlobalObject）再 JS_Eval。 */
TEST(Compiler, EvalObjectGetPrototypeOf) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx); /* 触发 installBuiltins */
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var o = {}; var p = Object.getPrototypeOf(o);"
      "p === Object.prototype";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN) << "error: " << JS_ToString(ctx, r);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 无原型对象：Object.create(null) 的 getPrototypeOf -> null。 */
TEST(Compiler, EvalCreateNullProto) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var o = Object.create(null);"
      "var p = Object.getPrototypeOf(o);"
      "p === null";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN) << "error: " << JS_ToString(ctx, r);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 原型继承读取：Object.create({x: 1}).x -> 1（属性沿原型链解析）。 */
TEST(Compiler, EvalCreateInheritProp) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src = "var o = Object.create({x: 1}); o.x";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 原型链多级继承：var p1 = {x: 1}; var p2 = Object.create(p1);
 * var o = Object.create(p2); o.x -> 1（沿链上溯）。 */
TEST(Compiler, EvalCreateMultiLevel) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var p1 = {x: 1}; var p2 = Object.create(p1);"
      "var o = Object.create(p2); o.x";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 对象构造器：Object({x: 5}) 返回原对象；Object() 新建普通对象。 */
TEST(Compiler, EvalObjectConstructor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src = "var o = {x: 5}; var w = Object(o); w === o";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN) << "error: " << JS_ToString(ctx, r);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
  JS_FreeValue(ctx, r);

  const char *src2 = "var n = Object(); var p = Object.getPrototypeOf(n);"
                     "p === Object.prototype";
  JSValue *r2 = JS_Eval(ctx, src2, std::strlen(src2));
  ASSERT_NE(r2, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r2), JS_BOOLEAN)
      << "error: " << JS_ToString(ctx, r2);
  EXPECT_EQ(JS_ToBoolean(ctx, r2), 1);
  JS_FreeValue(ctx, r2);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Object.setPrototypeOf：var o = {a: 1}; var p = {b: 2};
 * Object.setPrototypeOf(o, p); o.b -> 2（原型链切换生效）。 */
TEST(Compiler, EvalSetPrototypeOfInherit) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var o = {a: 1}; var p = {b: 2}; Object.setPrototypeOf(o, p); o.b";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 2.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 循环原型链防护：var a = {}; var b = {};
 * Object.setPrototypeOf(a, b); Object.setPrototypeOf(b, a) -> TypeError。 */
TEST(Compiler, EvalSetPrototypeOfCycle) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = {}; var b = {}; Object.setPrototypeOf(a, b);"
      "Object.setPrototypeOf(b, a); 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "cycle not detected: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Object.getPrototypeOf 对原始值抛 TypeError：getPrototypeOf(5) -> 异常。 */
TEST(Compiler, EvalGetPrototypeOfPrimitive) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src = "Object.getPrototypeOf(5); 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
      << "primitive not rejected: " << JS_ToString(ctx, r);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* GC 压力下原型链存活：Object.create({x: 1}) + 原型引用进 GC 标记。
 * 原型对象仅被子对象引用，GC 后仍需可读。 */
TEST(Compiler, EvalPrototypeUnderGC) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = ic(ctx);
  c->heap().setGcThreshold(4);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var p = {x: 1}; var o = Object.create(p);"
      "var s = 0; var i = 0; while (i < 20) { s = s + o.x; i = i + 1; } s";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 20.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：数组字面量的原型链指向 Array.prototype，且方法可调用。 */
TEST(Compiler, EvalArrayProtoChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "Object.getPrototypeOf([1, 2]) === Array.prototype ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：Array() 构造器预分配 length，与数组字面量等价。 */
TEST(Compiler, EvalArrayConstructorLength) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = Array(5); var b = Array(1, 2, 3);"
      "(a.length === 5 && b.length === 3 && b[2] === 3) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：push/pop 修改 length 并返回正确值。 */
TEST(Compiler, EvalArrayPushPop) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = []; var n = a.push(1, 2, 3);"
      "var p = a.pop();"
      "(n === 3 && p === 3 && a.length === 2) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：join / toString / reverse / indexOf。 */
TEST(Compiler, EvalArrayMethods) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = [1, 2, 3];"
      "var j = a.join('-');"
      "var t = a.toString();"
      "a.reverse();"
      "var i = a.indexOf(2);"
      "(j === '1-2-3' && t === '1,2,3' &&"
      " a[0] === 3 && a[1] === 2 && i === 1) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：Array.isArray + shift/unshift。 */
TEST(Compiler, EvalArrayIsArrayShiftUnshift) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = [2, 3];"
      "var s = a.shift();"
      "var n = a.unshift(1);"
      "var ia = Array.isArray(a);"
      "var io = Array.isArray({});"
      "(s === 2 && n === 2 && a[0] === 1 && a[1] === 3 &&"
      " ia === true && io === false) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.1：GC 压力下数组字面量与 Array.prototype 引用存活。 */
TEST(Compiler, EvalArrayProtoUnderGC) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::JSContext *c = ic(ctx);
  c->heap().setGcThreshold(4);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var a = [1, 2, 3]; var s = 0; var i = 0;"
      "while (i < 20) { s = s + a.length + a[0]; i = i + 1; } s";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 80.0);
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M4.2 Function/Error 家族                                                   */
/* -------------------------------------------------------------------------- */

/* new 语义（M4.2）：new 内置构造器，新对象原型 = 构造器.prototype。 */
TEST(Compiler, EvalNew) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx); /* 触发 installBuiltins */
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var o = new Object();"
      "var isObj = (typeof o === 'object' && o !== null);"
      "var p = Object.getPrototypeOf(o);"
      "var protoOk = (p === Object.prototype);"
      "isObj && protoOk ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* new Array(3)：预分配 length 的数组构造。 */
TEST(Compiler, EvalNewArray) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var b = new Array(3);"
      "(b.length === 3 && b[0] === undefined) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Function 构造器（M4.2）：Function() 产生可调用空函数，调用返回 undefined。 */
TEST(Compiler, EvalFunctionConstructor) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var f = new Function();"
      "var r = f();"
      "(r === undefined && typeof f === 'function') ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Function.prototype.call（M4.2）：func.call(thisArg, ...args) 转发参数。 */
TEST(Compiler, EvalFunctionPrototypeCall) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var f = function(a, b) { return a * 10 + b; };"
      "var r = f.call(null, 3, 4);"
      "(r === 34) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Function.prototype.apply（M4.2）：func.apply(thisArg, arr) 展开数组参数。 */
TEST(Compiler, EvalFunctionApply) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var f = function(a, b) { return a * 10 + b; };"
      "var r = f.apply(null, [7, 8]);"
      "(r === 78) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Function.prototype.bind（M4.2）：绑定参数 + 调用参数拼接（§20.2.3.5）。 */
TEST(Compiler, EvalFunctionBind) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var f = function(a, b, c) { return a * 100 + b * 10 + c; };"
      "var g = f.bind(null, 1, 2);"
      "var r = g(3);"
      "(r === 123) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Error 家族（M4.2）：Error 构造 + message/name 属性 + 原型链。 */
TEST(Compiler, EvalErrorFamily) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var e = new Error('boom');"
      "e.message";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "boom");
  JS_FreeValue(ctx, r);

  const char *src2 =
      "var e = new TypeError('bad');"
      "e.name";
  JSValue *r2 = JS_Eval(ctx, src2, std::strlen(src2));
  ASSERT_NE(r2, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r2), JS_STRING) << "error: " << JS_ToString(ctx, r2);
  EXPECT_STREQ(JS_ToString(ctx, r2), "TypeError");
  JS_FreeValue(ctx, r2);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Error 家族原型链（M4.2）：TypeError.prototype -> Error.prototype。 */
TEST(Compiler, EvalErrorProtoChain) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var tp = Object.getPrototypeOf(TypeError.prototype);"
      "(tp === Error.prototype) ? 'ok' : 'bad'";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "ok");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* Error.prototype.toString（M4.2）："name: message"。 */
TEST(Compiler, EvalErrorToString) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var e = new RangeError('oops');"
      "e.toString()";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING) << "error: " << JS_ToString(ctx, r);
  EXPECT_STREQ(JS_ToString(ctx, r), "RangeError: oops");
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* GC 压力下 M4.2 对象（new Error / bind 函数）引用存活。 */
TEST(Compiler, EvalM42UnderGC) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);
  neojs::JSContext *c = ic(ctx);
  c->heap().setGcThreshold(4);
  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  const char *src =
      "var e = new Error('x');"
      "var f = function(a){return a+1}; var g = f.bind(null, 1);"
      "var s = 0; var i = 0;"
      "while (i < 20) { s = s + (e.message === 'x' ? 1 : 0) + g(); i = i + 1; } s";
  JSValue *r = JS_Eval(ctx, src, std::strlen(src));
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
  EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 60.0); /* (1 + 2) * 20 */
  JS_FreeValue(ctx, r);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* -------------------------------------------------------------------------- */
/* M4.3c：BigInt 转换抽象运算（直接验证 ToString/ToNumber/ToBoolean +        */
/* 相等/关系比较的 BigInt 分支）。                                             */
/* -------------------------------------------------------------------------- */

/* JS_CreateBigInt + JS_ToString：BigInt -> 十进制字符串（§6.1.6.2.21）。 */
TEST(Conversion, BigIntToString) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *b1 = JS_CreateBigInt(ctx, 0, "12345", nullptr);
  ASSERT_NE(b1, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, b1), "12345");

  JSValue *b2 = JS_CreateBigInt(ctx, 1, "12345", nullptr);
  ASSERT_NE(b2, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, b2), "-12345");

  JSValue *b3 = JS_CreateBigInt(ctx, 0, "0", nullptr);
  ASSERT_NE(b3, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, b3), "0");

  /* 前导 0 归一化（JS_CreateBigInt 内部去前导 0）。 */
  JSValue *b4 = JS_CreateBigInt(ctx, 0, "00042", nullptr);
  ASSERT_NE(b4, nullptr);
  EXPECT_STREQ(JS_ToString(ctx, b4), "42");

  JS_FreeValue(ctx, b1);
  JS_FreeValue(ctx, b2);
  JS_FreeValue(ctx, b3);
  JS_FreeValue(ctx, b4);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_ToNumber(BigInt)：§7.1.4 规定 BigInt ToNumber 抛 TypeError；
 * JS_ToNumber 无法传错误，QuickJS 风格统一 NaN。 */
TEST(Conversion, BigIntToNumberFails) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *b = JS_CreateBigInt(ctx, 0, "42", nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_TRUE(std::isnan(JS_ToNumber(ctx, b)));

  JS_FreeValue(ctx, b);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* JS_ToBoolean(BigInt)：0n falsey，其余 truthy。 */
TEST(Conversion, BigIntToBoolean) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *z = JS_CreateBigInt(ctx, 0, "0", nullptr);
  JSValue *nz = JS_CreateBigInt(ctx, 0, "1", nullptr);
  JSValue *neg = JS_CreateBigInt(ctx, 1, "1", nullptr);
  ASSERT_NE(z, nullptr);
  ASSERT_NE(nz, nullptr);
  ASSERT_NE(neg, nullptr);
  EXPECT_EQ(JS_ToBoolean(ctx, z), 0);
  EXPECT_EQ(JS_ToBoolean(ctx, nz), 1);
  EXPECT_EQ(JS_ToBoolean(ctx, neg), 1); /* -1n truthy */

  JS_FreeValue(ctx, z);
  JS_FreeValue(ctx, nz);
  JS_FreeValue(ctx, neg);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 严格相等（§7.2.14）：同类型 BigInt 按值比较。 */
TEST(Conversion, BigIntStrictEqual) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *b1 = JS_CreateBigInt(ctx, 0, "123", nullptr);
  JSValue *b2 = JS_CreateBigInt(ctx, 0, "123", nullptr);
  JSValue *b3 = JS_CreateBigInt(ctx, 0, "124", nullptr);
  JSValue *b4 = JS_CreateBigInt(ctx, 1, "123", nullptr);
  JSValue *n123 = JS_CreateNumber(ctx, 123, nullptr);
  ASSERT_NE(b1, nullptr);
  ASSERT_NE(b2, nullptr);
  ASSERT_NE(b3, nullptr);
  ASSERT_NE(b4, nullptr);
  ASSERT_NE(n123, nullptr);

  EXPECT_EQ(JS_StrictEqual(ctx, b1, b2), 1);   /* 123n === 123n */
  EXPECT_EQ(JS_StrictEqual(ctx, b1, b3), 0);   /* 123n === 124n */
  EXPECT_EQ(JS_StrictEqual(ctx, b1, b4), 0);   /* 123n === -123n */
  EXPECT_EQ(JS_StrictEqual(ctx, b1, n123), 0); /* 123n === 123（类型不同） */

  JS_FreeValue(ctx, b1);
  JS_FreeValue(ctx, b2);
  JS_FreeValue(ctx, b3);
  JS_FreeValue(ctx, b4);
  JS_FreeValue(ctx, n123);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 抽象相等（§7.2.13 IsLooselyEqual）BigInt 分支。 */
TEST(Conversion, BigIntAbstractEqual) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *b1 = JS_CreateBigInt(ctx, 0, "123", nullptr);
  JSValue *n123 = JS_CreateNumber(ctx, 123, nullptr);
  JSValue *n1235 = JS_CreateNumber(ctx, 123.5, nullptr);
  JSValue *nBig = JS_CreateNumber(ctx, 1e30, nullptr);
  JSValue *nInf = JS_CreateNumber(ctx, INFINITY, nullptr);
  JSValue *nNaN = JS_CreateNumber(ctx, NAN, nullptr);
  JSValue *s123 = JS_CreateString(ctx, "123", std::strlen("123"), nullptr);
  JSValue *sBad = JS_CreateString(ctx, "1.5", std::strlen("1.5"), nullptr);
  JSValue *sBad2 = JS_CreateString(ctx, "abc", std::strlen("abc"), nullptr);
  JSValue *t = JS_CreateBoolean(ctx, 1, nullptr);
  ASSERT_NE(b1, nullptr);
  ASSERT_NE(n123, nullptr);
  ASSERT_NE(n1235, nullptr);
  ASSERT_NE(nBig, nullptr);
  ASSERT_NE(nInf, nullptr);
  ASSERT_NE(nNaN, nullptr);
  ASSERT_NE(s123, nullptr);
  ASSERT_NE(sBad, nullptr);
  ASSERT_NE(sBad2, nullptr);
  ASSERT_NE(t, nullptr);

  /* BigInt == Number：精确比较。 */
  JSValue *r = JS_Equal(ctx, b1, n123);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);   /* 123n == 123 */
  JS_FreeValue(ctx, r);
  r = JS_Equal(ctx, b1, n1235);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);   /* 123n == 123.5 */
  JS_FreeValue(ctx, r);

  /* 大整数 == 精确 Number（2^60 = 1152921504606846976）。 */
  JSValue *b60 = JS_CreateBigInt(ctx, 0, "1152921504606846976", nullptr);
  ASSERT_NE(b60, nullptr);
  JSValue *n2p60 = JS_CreateNumber(ctx, 1152921504606846976.0, nullptr);
  ASSERT_NE(n2p60, nullptr);
  r = JS_Equal(ctx, b60, n2p60);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
  JS_FreeValue(ctx, r);
  r = JS_Equal(ctx, b60, nBig); /* 1e30 != 2^60 */
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);
  JS_FreeValue(ctx, r);

  /* Infinity / NaN 与 BigInt 不等。 */
  r = JS_Equal(ctx, b1, nInf);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);
  JS_FreeValue(ctx, r);
  r = JS_Equal(ctx, b1, nNaN);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);
  JS_FreeValue(ctx, r);

  /* BigInt == String：String 转 BigInt。 */
  r = JS_Equal(ctx, b1, s123);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 123n == "123" */
  JS_FreeValue(ctx, r);
  r = JS_Equal(ctx, b1, sBad);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* "1.5" StringToBigInt 失败 -> false */
  JS_FreeValue(ctx, r);
  r = JS_Equal(ctx, b1, sBad2);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* "abc" -> false */
  JS_FreeValue(ctx, r);

  /* BigInt == Boolean：Boolean 先 ToNumber。 */
  r = JS_Equal(ctx, b1, t);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 123n == true (1) */
  JS_FreeValue(ctx, r);
  JSValue *b1n = JS_CreateBigInt(ctx, 0, "1", nullptr);
  ASSERT_NE(b1n, nullptr);
  r = JS_Equal(ctx, b1n, t);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 1n == true */
  JS_FreeValue(ctx, r);

  JS_FreeValue(ctx, b1);
  JS_FreeValue(ctx, b60);
  JS_FreeValue(ctx, b1n);
  JS_FreeValue(ctx, n123);
  JS_FreeValue(ctx, n1235);
  JS_FreeValue(ctx, nBig);
  JS_FreeValue(ctx, nInf);
  JS_FreeValue(ctx, nNaN);
  JS_FreeValue(ctx, s123);
  JS_FreeValue(ctx, sBad);
  JS_FreeValue(ctx, sBad2);
  JS_FreeValue(ctx, t);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 抽象关系比较（§7.2.12 IsLessThan）BigInt 分支。 */
TEST(Conversion, BigIntRelationalCompare) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *b1 = JS_CreateBigInt(ctx, 0, "10", nullptr);
  JSValue *b2 = JS_CreateBigInt(ctx, 0, "20", nullptr);
  JSValue *n15 = JS_CreateNumber(ctx, 15, nullptr);
  JSValue *n155 = JS_CreateNumber(ctx, 15.5, nullptr);
  JSValue *nNeg = JS_CreateNumber(ctx, -5, nullptr);
  JSValue *nInf = JS_CreateNumber(ctx, INFINITY, nullptr);
  JSValue *nNegInf = JS_CreateNumber(ctx, -INFINITY, nullptr);
  JSValue *s15 = JS_CreateString(ctx, "15", std::strlen("15"), nullptr);
  JSValue *sBad = JS_CreateString(ctx, "x", std::strlen("x"), nullptr);
  ASSERT_NE(b1, nullptr);
  ASSERT_NE(b2, nullptr);
  ASSERT_NE(n15, nullptr);
  ASSERT_NE(n155, nullptr);
  ASSERT_NE(nNeg, nullptr);
  ASSERT_NE(nInf, nullptr);
  ASSERT_NE(nNegInf, nullptr);
  ASSERT_NE(s15, nullptr);
  ASSERT_NE(sBad, nullptr);

  /* BigInt < BigInt。 */
  JSValue *r = JS_RelationalCompare(ctx, b1, b2);
  ASSERT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 10n < 20n */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b2, b1);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 20n < 10n */
  JS_FreeValue(ctx, r);

  /* BigInt < Number（整数 / 小数 / 负 / ±Infinity）。 */
  r = JS_RelationalCompare(ctx, b1, n15);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 10n < 15 */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b1, n155);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 10n < 15.5 */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b1, nNeg);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 10n < -5 */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, nNeg, b1);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* -5 < 10n */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b1, nInf);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 10n < +Infinity */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b1, nNegInf);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 10n < -Infinity */
  JS_FreeValue(ctx, r);

  /* 相等值（不同型）< 为 false。 */
  r = JS_RelationalCompare(ctx, b2, n15);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 20n < 15 */
  JS_FreeValue(ctx, r);
  JSValue *b15 = JS_CreateBigInt(ctx, 0, "15", nullptr);
  ASSERT_NE(b15, nullptr);
  r = JS_RelationalCompare(ctx, b15, n15);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* 15n < 15 */
  JS_FreeValue(ctx, r);

  /* BigInt < String：String 转 BigInt；失败 -> false（undefined 语义）。 */
  r = JS_RelationalCompare(ctx, b1, s15);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 1);  /* 10n < "15" */
  JS_FreeValue(ctx, r);
  r = JS_RelationalCompare(ctx, b1, sBad);
  EXPECT_EQ(JS_ToBoolean(ctx, r), 0);  /* "x" 非整数 -> false */
  JS_FreeValue(ctx, r);

  JS_FreeValue(ctx, b1);
  JS_FreeValue(ctx, b2);
  JS_FreeValue(ctx, b15);
  JS_FreeValue(ctx, n15);
  JS_FreeValue(ctx, n155);
  JS_FreeValue(ctx, nNeg);
  JS_FreeValue(ctx, nInf);
  JS_FreeValue(ctx, nNegInf);
  JS_FreeValue(ctx, s15);
  JS_FreeValue(ctx, sBad);
  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 端到端：BigInt 字面量 + 比较（经 JS_Eval 验证 VM PushBigInt + 比较指令）。 */
TEST(Compiler, EvalBigIntLiteralCompare) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* 0x10n = 16n > 10n；12345678901234567890n 超 64 位。 */
  struct { const char *src; double expect; } cases[] = {
      {"0x10n > 10n ? 1 : 0", 1.0},
      {"12345678901234567890n === 12345678901234567890n ? 1 : 0", 1.0},
      {"0n ? 1 : 0", 0.0},
      {"10n < 20n ? 1 : 0", 1.0},
      {"var a = 0x10n > 10n ? 1 : 0;"
       "var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0;"
       "var c = (0n ? 1 : 0);"
       "var d = 10n < 20n ? 1 : 0;"
       "a + b + c + d", 3.0},  /* c=0（0n falsy）：1+1+0+1=3 */
      {"var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0; b",
       1.0},
      {"var c = (0n ? 1 : 0); c", 0.0},
      {"var d = 10n < 20n ? 1 : 0; d", 1.0},
      {"var a = 0x10n > 10n ? 1 : 0; a", 1.0},
      {"0x10n > 10n ? 1 : 0 + (0n ? 1 : 0) + (10n < 20n ? 1 : 0)", 1.0},
      /* 上式因 + 优先级高于 ?:，解析为 (0x10n>10n) ? 1 : (0+0+1)，条件真 -> 1。 */
      {"(0x10n > 10n ? 1 : 0) + (0n ? 1 : 0) + (10n < 20n ? 1 : 0)", 2.0},
      {"var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0;"
       "var c = (0n ? 1 : 0);"
       "b + c", 1.0},
      {"var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0;"
       "var d = 10n < 20n ? 1 : 0;"
       "b + d", 2.0},
      {"var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0;"
       "var c = (0n ? 1 : 0);"
       "var d = 10n < 20n ? 1 : 0;"
       "b + c + d", 2.0},
      {"var b = 12345678901234567890n === 12345678901234567890n ? 1 : 0;"
       "var a = 0x10n > 10n ? 1 : 0;"
       "a + b", 2.0},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
        << "case " << i << " error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), cases[i].expect) << "case " << i;
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* 端到端：BigInt 运算符语义（M4.3d）——算术/位运算/一元取负/typeOf/拼接/错误。
 * 每个 case 期望返回 BigInt 的十进制串（"n" 结尾）或 Number/字符串/错误标记。 */
TEST(Compiler, EvalBigIntOperators) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* {src, expect}: expect "N" = BigInt 十进制串；"#num" = Number 值；
   * "#str:..." = 字符串；"#err:..." = 异常（含子串匹配）。 */
  struct { const char *src; const char *expect; } cases[] = {
      /* 算术：BigInt + BigInt -> BigInt。 */
      {"12345678901234567890n + 1n", "12345678901234567891"},
      {"12345678901234567890n - 10n", "12345678901234567880"},
      {"12345678901234567890n * 2n", "24691357802469135780"},
      {"10n / 3n", "3"},
      {"10n % 3n", "1"},
      {"-10n % 3n", "-1"},
      {"10n % -3n", "1"},      /* 余数符号同被除数 */
      {"2n ** 10n", "1024"},
      {"(-2n) ** 3n", "-8"},
      {"(-2n) ** 2n", "4"},
      {"0n ** 0n", "1"},
      /* 负数算术。 */
      {"-10n + 3n", "-7"},
      {"-10n - 3n", "-13"},
      {"-10n * -3n", "30"},
      {"-10n / -3n", "3"},
      /* 除零 / 负指数。 */
      {"1n / 0n", "#err:RangeError"},
      {"1n % 0n", "#err:RangeError"},
      {"2n ** -1n", "#err:RangeError"},
      /* 混 Number -> TypeError。 */
      {"1n + 1", "#err:TypeError"},
      {"1 + 1n", "#err:TypeError"},
      {"1n * 2", "#err:TypeError"},
      {"1n - 0.5", "#err:TypeError"},
      /* 位运算。 */
      {"5n & 3n", "1"},
      {"5n | 3n", "7"},
      {"5n ^ 3n", "6"},
      {"5n << 2n", "20"},
      {"20n >> 2n", "5"},
      {"-1n & 3n", "3"},   /* 补码：...111 & 011 = 011 */
      {"-1n | 3n", "-1"},
      {"-5n >> 1n", "-3"}, /* 算术右移向负无穷 */
      {"-8n >> 1n", "-4"},
      {"-1n >> 100n", "-1"},
      {"1n << 3n", "8"},
      {"1n << -1n", "0"},  /* 右移 1：1 >> 1 = 0 */
      {"4n >> -1n", "8"},  /* 左移 1 */
      /* >>> 对 BigInt 抛 TypeError。 */
      {"1n >>> 1n", "#err:TypeError"},
      {"5n & 3", "#err:TypeError"},
      {"1n | 0.5", "#err:TypeError"},
      /* 一元取负。 */
      {"-12345678901234567890n", "-12345678901234567890"},
      {"-0n", "0"},
      {"-(-5n)", "5"},
      /* typeof。 */
      {"typeof 1n", "#str:bigint"},
      /* 字符串拼接：BigInt + String / String + BigInt。 */
      {"12345678901234567890n + ''", "#str:12345678901234567890"},
      {"'x' + 10n", "#str:x10"},
      {"-10n + 'abc'", "#str:-10abc"},
      /* Number 路径回归。 */
      {"1 + 2", "#num:3"},
      {"7 & 3", "#num:3"},
      {"-5 >> 1", "#num:-3"},
      {"8 << 2", "#num:32"},
      {"-8 >>> 1", "#num:2147483644"},
      {"typeof 123", "#str:number"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " returned null for: " << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'e') { /* #err */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " expected exception for: " << cases[i].src
            << " got: " << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos) << "case " << i;
        }
      } else if (e[1] == 'n') { /* #num */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), std::atof(e + 5)) << "case " << i;
        }
      } else { /* #str */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING) << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5)) << "case " << i;
        }
      }
    } else {
      /* BigInt：kind 应为 JS_BIGINT，十进制串匹配。 */
      EXPECT_EQ(JS_GetKind(ctx, r), JS_BIGINT)
          << "case " << i << " expected BigInt for: " << cases[i].src
          << " got kind " << JS_GetKind(ctx, r) << ": " << JS_ToString(ctx, r);
      if (JS_GetKind(ctx, r) == JS_BIGINT) {
        EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e)) << "case " << i;
      }
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

TEST(Compiler, EvalBigIntBuiltins) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  /* expect 格式同 EvalBigIntOperators：
   * "N" = BigInt 十进制串；"#num:x" = Number；"#str:x" = 字符串；
   * "#obj:x" = 对象（装箱，x 为 typeof）；"#err:x" = 异常（子串匹配）。 */
  struct { const char *src; const char *expect; } cases[] = {
      /* BigInt 构造器（§21.2.1.1）：BigInt(x) 类型转换。 */
      {"BigInt(42)", "42"},
      {"BigInt(-123)", "-123"},
      {"BigInt(0)", "0"},
      {"BigInt('123')", "123"},
      {"BigInt('0x1f')", "31"},
      {"BigInt('0o17')", "15"},
      {"BigInt('0b101')", "5"},
      {"BigInt('  42  ')", "42"},       /* 首尾空白 */
      {"BigInt('1_000')", "1000"},      /* 数字分隔符 */
      {"BigInt(true)", "1"},
      {"BigInt(false)", "0"},
      {"BigInt(10n)", "10"},
      /* new BigInt() 禁止（§21.2.1 步骤 1：BigInt 不是构造器）。 */
      {"new BigInt(1)", "#err:TypeError"},
      {"new BigInt()", "#err:TypeError"},
      /* 构造器转换错误。 */
      {"BigInt(1.5)", "#err:RangeError"},        /* Number 非整数 -> RangeError */
      {"BigInt(Infinity)", "#err:RangeError"},
      {"BigInt('1.5')", "#err:SyntaxError"},     /* 小数串 -> SyntaxError */
      {"BigInt('abc')", "#err:SyntaxError"},
      {"BigInt()", "#err:TypeError"},            /* 无参 -> ToBigInt(undefined) TypeError */
      {"BigInt(undefined)", "#err:TypeError"},
      {"BigInt(null)", "#err:TypeError"},
      /* BigInt.prototype.valueOf（§21.2.3.4）。 */
      {"BigInt.prototype.valueOf.call(10n)", "10"},
      {"BigInt.prototype.valueOf.call(BigInt('123'))", "123"},
      {"BigInt.prototype.valueOf.call(1)", "#err:TypeError"},
      /* BigInt.prototype.toString（§21.2.3.3）：十进制默认。 */
      {"BigInt.prototype.toString.call(123n)", "#str:123"},
      {"BigInt.prototype.toString.call(-123n)", "#str:-123"},
      {"(123n).toString()", "#str:123"},
      {"(-123n).toString()", "#str:-123"},
      {"(123n).toString(10)", "#str:123"},
      {"(123n).toString(2)", "#err:not implemented"},   /* 非十进制本阶段未实现 */
      {"(123n).toString(1)", "#err:RangeError"},
      {"(123n).toString(37)", "#err:RangeError"},
      {"(123n).toString(2.5)", "#err:RangeError"},
      {"BigInt.prototype.toString.call(1)", "#err:TypeError"},
      /* BigInt 装箱（ToObject，§7.1.13）：原始值转包装对象。 */
      {"typeof Object(10n)", "#str:object"},
      {"typeof Object(10n).valueOf()", "#str:bigint"},
      {"Object(10n).valueOf()", "10"},
      {"typeof 10n.valueOf()", "#str:bigint"},
      {"10n.valueOf()", "10"},
      /* 原型链：装箱对象继承 BigInt.prototype 方法。 */
      {"Object(10n).toString()", "#str:10"},
      {"Object(10n).valueOf() + 1n", "11"},
      /* typeof 保持 bigint。 */
      {"typeof BigInt('9')", "#str:bigint"},
      /* 全局 BigInt 是函数（typeof function）。 */
      {"typeof BigInt", "#str:function"},
      /* BigInt.prototype.constructor === BigInt。 */
      {"BigInt.prototype.constructor === BigInt", "#bool:1"},
      {"BigInt.prototype.constructor === Number", "#bool:0"},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " returned null for: " << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'e') { /* #err */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " expected exception for: " << cases[i].src
            << " got: " << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos) << "case " << i << " msg=" << msg;
        }
      } else if (e[1] == 'n') { /* #num */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), std::atof(e + 5)) << "case " << i;
        }
      } else if (e[1] == 's') { /* #str */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING) << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5)) << "case " << i;
        }
      } else if (e[1] == 'b') { /* #bool */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN) << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_BOOLEAN) {
          /* #bool:x 前缀是 "#bool:"（6 字符），值从 e+6 开始。 */
          EXPECT_EQ(JS_ToBoolean(ctx, r), std::atoi(e + 6)) << "case " << i << " src=" << cases[i].src;
        }
      } else { /* #obj */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_OBJECT) << "case " << i << " src=" << cases[i].src;
      }
    } else {
      /* BigInt：kind 应为 JS_BIGINT，十进制串匹配。 */
      EXPECT_EQ(JS_GetKind(ctx, r), JS_BIGINT)
          << "case " << i << " expected BigInt for: " << cases[i].src
          << " got kind " << JS_GetKind(ctx, r) << ": " << JS_ToString(ctx, r);
      if (JS_GetKind(ctx, r) == JS_BIGINT) {
        EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e)) << "case " << i;
      }
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.3g：Number.prototype 方法补齐 + Number 静态方法（§21.1.2/§21.1.3）。
 * expect 前缀：#num: 数字；#str: 字符串；#bool: 布尔；#err: 异常子串。 */
TEST(Compiler, EvalNumberProtoMethods) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; const char *expect; } cases[] = {
      /* Number.prototype.toString：radix 支持（§21.1.3.6）。 */
      {"(255).toString(16)", "#str:ff"},
      {"(255).toString(2)", "#str:11111111"},
      {"(255).toString(8)", "#str:377"},
      {"(255).toString()", "#str:255"},
      {"(255).toString(10)", "#str:255"},
      {"(-255).toString(16)", "#str:-ff"},
      {"(0).toString(2)", "#str:0"},
      {"(3.14).toString(2)", "#str:3.14"},        /* 小数回退十进制 */
      {"(255).toString(1)", "#err:RangeError"},
      {"(255).toString(37)", "#err:RangeError"},
      {"(NaN).toString()", "#str:NaN"},
      {"(Infinity).toString()", "#str:Infinity"},
      {"(1e21).toString()", "#str:1e+21"},
      /* toFixed（§21.1.3.5）。 */
      {"(123.456).toFixed(2)", "#str:123.46"},
      {"(123.456).toFixed(0)", "#str:123"},
      {"(0.1).toFixed(2)", "#str:0.10"},
      {"(123.456).toFixed()", "#str:123"},
      {"(-123.456).toFixed(1)", "#str:-123.5"},
      {"(1e21).toFixed(2)", "#str:1e+21"},          /* 大数回退 */
      {"(123).toFixed(-1)", "#err:RangeError"},
      {"(123).toFixed(101)", "#err:RangeError"},
      /* toExponential（§21.1.3.2）。 */
      {"(123.456).toExponential(2)", "#str:1.23e+2"},
      {"(123.456).toExponential()", "#str:1.23456e+2"},
      {"(0).toExponential(2)", "#str:0.00e+0"},
      {"(100).toExponential(1)", "#str:1.0e+2"},
      {"(9.99).toExponential(1)", "#str:1.0e+1"},        /* 进位 */
      {"(999).toExponential(2)", "#str:9.99e+2"},
      {"(999).toExponential(1)", "#str:1.0e+3"},        /* 进位提升指数 */
      {"(-123.456).toExponential(2)", "#str:-1.23e+2"},
      {"(1e-7).toExponential()", "#str:1e-7"},           /* 小指数 */
      {"(123.456).toExponential(0)", "#str:1e+2"},
      {"(123.456).toExponential(6)", "#str:1.234560e+2"}, /* 补零 */
      {"(0.000001).toExponential(2)", "#str:1.00e-6"},    /* 负指数 */
      {"(1e100).toExponential(2)", "#str:1.00e+100"},     /* 大指数 */
      /* toPrecision（§21.1.3.3）。 */
      {"(123.456).toPrecision(4)", "#str:123.5"},
      {"(123.456).toPrecision(2)", "#str:1.2e+2"},
      {"(123.456).toPrecision()", "#str:123.456"},
      {"(0.000123).toPrecision(2)", "#str:0.00012"},
      {"(9.99).toPrecision(2)", "#str:10"},              /* 进位+定点 */
      {"(1234).toPrecision(2)", "#str:1.2e+3"},
      {"(-123.456).toPrecision(4)", "#str:-123.5"},
      {"(1).toPrecision(21)", "#str:1.00000000000000000000"},
      {"(1).toPrecision(2)", "#str:1.0"},                /* 整数补零 */
      {"(1.5).toPrecision(2)", "#str:1.5"},
      {"(100).toPrecision(2)", "#str:1.0e+2"},
      {"(0.5).toPrecision(4)", "#str:0.5000"},           /* 小数补零 */
      {"(1e-5).toPrecision(3)", "#str:0.0000100"},
      {"(0).toPrecision(3)", "#str:0.00"},
      {"(1234).toPrecision(5)", "#str:1234.0"},
      {"(123.456).toPrecision(6)", "#str:123.456"},
      {"(123456789).toPrecision(2)", "#str:1.2e+8"},
      {"(123).toPrecision(1)", "#str:1e+2"},
      {"(123).toPrecision(0)", "#err:RangeError"},
      {"(123).toPrecision(101)", "#err:RangeError"},
      /* toLocaleString（§21.1.3.4）：等价 toString。 */
      {"(1234567).toLocaleString()", "#str:1234567"},
      {"(0.5).toLocaleString()", "#str:0.5"},
      /* 静态方法（§21.1.2）。 */
      {"Number.isFinite(42)", "#bool:1"},
      {"Number.isFinite('42')", "#bool:0"},          /* 不做类型转换 */
      {"Number.isFinite(Infinity)", "#bool:0"},
      {"Number.isFinite(NaN)", "#bool:0"},
      {"Number.isInteger(42)", "#bool:1"},
      {"Number.isInteger(3.14)", "#bool:0"},
      {"Number.isInteger('42')", "#bool:0"},
      {"Number.isInteger(-0)", "#bool:1"},
      {"Number.isNaN(NaN)", "#bool:1"},
      {"Number.isNaN('NaN')", "#bool:0"},
      {"Number.isNaN(42)", "#bool:0"},
      {"Number.isSafeInteger(42)", "#bool:1"},
      {"Number.isSafeInteger(9007199254740991)", "#bool:1"},
      {"Number.isSafeInteger(9007199254740992)", "#bool:0"},
      {"Number.isSafeInteger(3.5)", "#bool:0"},
      {"Number.isSafeInteger('42')", "#bool:0"},
      /* parseFloat（§21.1.2.12）。 */
      {"Number.parseFloat('3.14')", "#num:3.14"},
      {"Number.parseFloat('  3.14xyz')", "#num:3.14"},
      {"Number.parseFloat('abc')", "#num:NaN"},
      {"Number.parseFloat('Infinity')", "#num:Infinity"},
      {"Number.parseFloat('')", "#num:NaN"},
      /* parseInt（§21.1.2.13）。 */
      {"Number.parseInt('42')", "#num:42"},
      {"Number.parseInt('42px')", "#num:42"},
      {"Number.parseInt('  42  ')", "#num:42"},
      {"Number.parseInt('0x1F')", "#num:31"},
      {"Number.parseInt('0b101')", "#num:0"},        /* 二进制前缀不识别 */
      {"Number.parseInt('ff', 16)", "#num:255"},
      {"Number.parseInt('11', 2)", "#num:3"},
      {"Number.parseInt('abc')", "#num:NaN"},
      {"Number.parseInt('-42')", "#num:-42"},
      {"Number.parseInt('42', 1)", "#num:NaN"},
      {"Number.parseInt('42', 37)", "#num:NaN"},
      /* 常量属性（§21.1.2.1/§21.1.2.6-15）。 */
      {"Number.EPSILON > 0", "#bool:1"},
      {"Number.MAX_SAFE_INTEGER", "#num:9007199254740991"},
      {"Number.MIN_SAFE_INTEGER", "#num:-9007199254740991"},
      {"Number.MAX_VALUE > 0", "#bool:1"},
      {"Number.MIN_VALUE > 0", "#bool:1"},
      {"Number.NaN !== NaN", "#bool:1"},
      {"Number.POSITIVE_INFINITY === Infinity", "#bool:1"},
      {"Number.NEGATIVE_INFINITY === -Infinity", "#bool:1"},
      /* 原型链：装箱对象继承 Number.prototype 方法。 */
      {"Object(123).toFixed(1)", "#str:123.0"},
      {"Object(255).toString(16)", "#str:ff"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " src=" << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'e') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " src=" << cases[i].src << " got: "
            << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos)
              << "case " << i << " msg=" << msg;
        }
      } else if (e[1] == 'n') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          double want = std::atof(e + 5);
          double got = JS_ToNumber(ctx, r);
          if (std::isnan(want)) {
            EXPECT_TRUE(std::isnan(got))
                << "case " << i << " src=" << cases[i].src << " got=" << got;
          } else {
            EXPECT_DOUBLE_EQ(got, want) << "case " << i << " src=" << cases[i].src;
          }
        }
      } else if (e[1] == 's') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'b') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_BOOLEAN) {
          EXPECT_EQ(JS_ToBoolean(ctx, r), std::atoi(e + 6))
              << "case " << i << " src=" << cases[i].src;
        }
      }
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4b：Map 基本操作（§24.1）。用 JS_Eval 端到端验证
 * set/get/has/delete/clear/size/forEach 与构造器（数组 iterable）。 */
TEST(Compiler, EvalMapBasic) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; double num; } ncases[] = {
      /* 基本 set/get/size。 */
      {"var m = new Map(); m.set('a', 1); m.get('a')", 1.0},
      {"var m = new Map(); m.set('a', 1); m.size", 1.0},
      {"var m = new Map(); m.set('a', 1).set('b', 2); m.size", 2.0},
      /* 键覆盖：同键更新值，size 不变。 */
      {"var m = new Map(); m.set('a', 1); m.set('a', 9); m.get('a')", 9.0},
      {"var m = new Map(); m.set('a', 1); m.set('a', 9); m.size", 1.0},
      /* 对象键：引用相等。 */
      {"var o = {}; var m = new Map(); m.set(o, 7); m.get(o)", 7.0},
  };
  for (auto &c : ncases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), c.num) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; const char *str; } scases[] = {
      /* 字符串返回值。 */
      {"var m = new Map(); m.set('a', 'hello'); m.get('a')", "hello"},
      {"var m = new Map(); m.set(1, 'x').set('1', 'y'); m.get('1')", "y"},
      /* 数字键与字符串键独立：get(1) 取数字键 'x'。 */
      {"var m = new Map(); m.set(1, 'x').set('1', 'y'); m.get(1)", "x"},
  };
  for (auto &c : scases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_STREQ(JS_ToString(ctx, r), c.str) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; int b; } bcases[] = {
      /* 布尔返回值。 */
      {"var m = new Map(); m.set('a', 1); m.has('zzz')", 0},
      {"var m = new Map(); m.set('a', 1); m.has('a')", 1},
      {"var m = new Map(); m.set(NaN, 42); m.has(NaN)", 1},  /* SameValueZero */
      {"var m = new Map(); m.set(0, 'z'); m.has(-0)", 1},    /* ±0 归并 */
      {"var m = new Map(); m.set('a', 1) instanceof Map", 1},
  };
  for (auto &c : bcases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    EXPECT_EQ(JS_ToBoolean(ctx, r), c.b) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  /* delete/clear/has。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var m = new Map(); m.set('a', 1).set('b', 2); "
             "var d = m.delete('a'); m.has('a') === false && d && m.size === 1",
        std::strlen("var m = new Map(); m.set('a', 1).set('b', 2); "
                     "var d = m.delete('a'); m.has('a') === false && d && m.size === 1"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
    JS_FreeValue(ctx, r);
  }
  {
    JSValue *r = JS_Eval(
        ctx, "var m = new Map(); m.set('a', 1); m.clear(); m.size === 0",
        std::strlen("var m = new Map(); m.set('a', 1); m.clear(); m.size === 0"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
    JS_FreeValue(ctx, r);
  }
  /* 构造器数组 iterable：[[k1,v1],[k2,v2]]。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var m = new Map([['a', 1], ['b', 2]]); m.get('a') + m.get('b')",
        std::strlen("var m = new Map([['a', 1], ['b', 2]]); m.get('a') + m.get('b')"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
    JS_FreeValue(ctx, r);
  }
  /* forEach：value/key/map 三参 + thisArg。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var m = new Map(); m.set('a', 1).set('b', 2); var s = 0; "
             "m.forEach(function(v, k) { s = s + v; }); s",
        std::strlen("var m = new Map(); m.set('a', 1).set('b', 2); var s = 0; "
                     "m.forEach(function(v, k) { s = s + v; }); s"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4c：Set 构造器与 Set.prototype（§24.2）。 */
TEST(Compiler, EvalSetBasic) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; double num; } ncases[] = {
      /* 基本 add/has/size。 */
      {"var s = new Set(); s.add('a'); s.size", 1.0},
      {"var s = new Set(); s.add('a').add('b'); s.size", 2.0},
      /* 元素覆盖：同值重复 add，size 不变。 */
      {"var s = new Set(); s.add('a'); s.add('a'); s.size", 1.0},
      /* 对象元素：引用相等。 */
      {"var o = {}; var s = new Set(); s.add(o); s.size", 1.0},
  };
  for (auto &c : ncases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), c.num) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; const char *str; } scases[] = {
      /* 构造器数组 iterable。 */
      {"var s = new Set([1, 2, 3]); s.size === 3 ? 'ok' : 'bad'", "ok"},
  };
  for (auto &c : scases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_STREQ(JS_ToString(ctx, r), c.str) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; int b; } bcases[] = {
      /* 布尔返回值。 */
      {"var s = new Set(); s.add('a'); s.has('zzz')", 0},
      {"var s = new Set(); s.add('a'); s.has('a')", 1},
      {"var s = new Set(); s.add(NaN); s.has(NaN)", 1}, /* SameValueZero */
      {"var s = new Set(); s.add(0); s.has(-0)", 1},     /* ±0 归并 */
      {"var s = new Set(); s.add('a') instanceof Set", 1},
      /* 数字与字符串元素独立。 */
      {"var s = new Set(); s.add(1).add('1'); s.size === 2", 1},
  };
  for (auto &c : bcases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    EXPECT_EQ(JS_ToBoolean(ctx, r), c.b) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  /* delete/clear。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var s = new Set(); s.add('a').add('b'); "
             "var d = s.delete('a'); s.has('a') === false && d && s.size === 1",
        std::strlen("var s = new Set(); s.add('a').add('b'); "
                     "var d = s.delete('a'); s.has('a') === false && d && s.size === 1"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
    JS_FreeValue(ctx, r);
  }
  {
    JSValue *r = JS_Eval(
        ctx, "var s = new Set(); s.add('a'); s.clear(); s.size === 0",
        std::strlen("var s = new Set(); s.add('a'); s.clear(); s.size === 0"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(JS_ToBoolean(ctx, r), 1);
    JS_FreeValue(ctx, r);
  }
  /* forEach：value/key/set 三参（key 与 value 相同）+ 顶层 var 捕获。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var s = new Set(); s.add(1).add(2).add(3); var sum = 0; "
             "s.forEach(function(v, k) { sum = sum + v; }); sum",
        std::strlen("var s = new Set(); s.add(1).add(2).add(3); var sum = 0; "
                     "s.forEach(function(v, k) { sum = sum + v; }); sum"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 6.0);
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4d：Symbol 属性键（§7.3.22）。 */
TEST(Compiler, EvalSymbolPropertyKeys) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; double num; } ncases[] = {
      /* StoreElem/LoadElem Symbol 键读写。 */
      {"var s = Symbol('x'); var o = {}; o[s] = 42; o[s]", 42.0},
      /* 引用相等：同 Symbol 命中，新 Symbol 不命中（undefined->0）。 */
      {"var s = Symbol(); var o = {}; o[s] = 1; o[s] + (o[Symbol()] ? 10 : 0)", 1.0},
      /* Symbol 与字符串键独立。 */
      {"var s = Symbol('k'); var o = {}; o[s] = 1; o['k'] = 2; o[s] + o['k']", 3.0},
  };
  for (auto &c : ncases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), c.num) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; int b; } bcases[] = {
      /* 引用相等：同 Symbol 命中，新 Symbol 未命中（undefined）。 */
      {"var s = Symbol(); var o = {}; o[s] = 1; o[s] === 1", 1},
      {"var s = Symbol(); var o = {}; o[s] === undefined", 1},
      {"var s = Symbol(); var o = {}; o[s] = 1; o[Symbol()] === undefined", 1},
      /* Symbol 与字符串键独立。 */
      {"var s = Symbol('k'); var o = {}; o[s] = 1; o['k'] = 2; "
       "o[s] === 1 && o['k'] === 2", 1},
  };
  for (auto &c : bcases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    EXPECT_EQ(JS_ToBoolean(ctx, r), c.b) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  /* Symbol.iterator 是 well-known 全局，可读。 */
  {
    JSValue *r = JS_Eval(ctx, "Symbol.iterator",
                         std::strlen("Symbol.iterator"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_SYMBOL) << "got=" << JS_ToString(ctx, r);
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4f：for-of 编译执行 + 内建迭代器（§13.7.5.6 + §7.4）。 */
TEST(Compiler, EvalForOf) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; double num; } ncases[] = {
      /* 数组迭代求和。 */
      {"var s = 0; for (var x of [1, 2, 3]) { s = s + x; } s", 6.0},
      /* 空数组不迭代。 */
      {"var s = 0; for (var x of []) { s = s + 1; } s", 0.0},
      /* 已有 var 声明（不重声明，直接赋值）。 */
      {"var x; var s = 0; for (x of [1, 2, 3]) { s = s + x; } s", 6.0},
      /* 嵌套 for-of（1*10+1*20+2*10+2*20=90）。 */
      {"var s = 0; for (var x of [1, 2]) for (var y of [10, 20]) s = s + x * y; s",
       90.0},
      /* 字符串迭代：code point（含增补平面代理对合并）。 */
      {"var s = 0; for (var c of 'abc') { s = s + 1; } s", 3.0},
      {"var s = ''; for (var c of 'ab') { s = s + c; } s === 'ab' ? 1 : 0", 1.0},
      /* Set 迭代。 */
      {"var s = new Set([1, 2, 3]); var sum = 0; for (var x of s) sum = sum + x; sum",
       6.0},
      /* 自定义可迭代对象：Symbol.iterator 返回手工迭代器。
       * （this 表达式未实现，用闭包变量 i 替代 this.i）。 */
      {"var i = 0; var it = { next: function() { i = i + 1; return i <= 3 ? "
       "{ value: i * 10, done: false } : { value: undefined, done: true }; } }; "
       "var o = {}; o[Symbol.iterator] = function() { return it; }; "
       "var s = 0; for (var x of o) s = s + x; s", 60.0},
  };
  for (auto &c : ncases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), c.num) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  struct { const char *src; const char *str; } scases[] = {
      /* 字符串迭代拼接。 */
      {"var s = ''; for (var c of 'xyz') s = s + c; s", "xyz"},
      /* 增补平面：代理对合并为单个码点（真实输出为 😀）。 */
      {"var s = ''; for (var c of '\\u{1F600}') s = s + c; s", "\xF0\x9F\x98\x80"},
  };
  for (auto &c : scases) {
    JSValue *r = JS_Eval(ctx, c.src, std::strlen(c.src));
    ASSERT_NE(r, nullptr) << "case src=" << c.src;
    ASSERT_EQ(JS_GetKind(ctx, r), JS_STRING)
        << "case src=" << c.src << " got=" << JS_ToString(ctx, r);
    EXPECT_STREQ(JS_ToString(ctx, r), c.str) << "case src=" << c.src;
    JS_FreeValue(ctx, r);
  }

  /* 未实现 for-in：编译失败返回 Exception（M4.4f 未实现 ForIn）。 */
  {
    JSValue *r = JS_Eval(ctx, "for (var k in {a: 1}) {}",
                         std::strlen("for (var k in {a: 1}) {}"));
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
        << "got=" << JS_ToString(ctx, r);
    JS_FreeValue(ctx, r);
  }

  /* 数组字面量内建迭代器可直接调用 next()。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var it = [1, 2][Symbol.iterator](); var a = it.next().value; "
             "var b = it.next().value; var d = it.next().done; a + b + (d ? 10 : 0)",
        std::strlen("var it = [1, 2][Symbol.iterator](); var a = it.next().value; "
                     "var b = it.next().value; var d = it.next().done; a + b + (d ? 10 : 0)"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 13.0);
    JS_FreeValue(ctx, r);
  }

  /* Map for-of（entries 迭代器产出值）。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var m = new Map(); m.set('a', 1).set('b', 2); var s = 0; "
             "for (var v of m) s = s + v; s",
        std::strlen("var m = new Map(); m.set('a', 1).set('b', 2); var s = 0; "
                     "for (var v of m) s = s + v; s"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 3.0);
    JS_FreeValue(ctx, r);
  }

  /* 字符串迭代器手工推进（含 done）。 */
  {
    JSValue *r = JS_Eval(
        ctx, "var it = 'ab'[Symbol.iterator](); var a = it.next().value; "
             "var b = it.next().value; var d = it.next().done; "
             "(a === 'a' && b === 'b' && d) ? 1 : 0",
        std::strlen("var it = 'ab'[Symbol.iterator](); var a = it.next().value; "
                     "var b = it.next().value; var d = it.next().done; "
                     "(a === 'a' && b === 'b' && d) ? 1 : 0"));
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(JS_GetKind(ctx, r), JS_NUMBER) << "error: " << JS_ToString(ctx, r);
    EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), 1.0);
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4g：Object 静态方法（§20.1.2）。expect 前缀：#num / #str / #bool /
 * #arr: 数组转串（逗号拼接）；#err: 异常子串。 */
TEST(Compiler, EvalObjectStaticMethods) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; const char *expect; } cases[] = {
      /* Object.keys / values / entries。 */
      {"Object.keys({a: 1, b: 2}).join(',')", "#str:a,b"},
      {"Object.values({a: 1, b: 2}).join(',')", "#str:1,2"},
      {"Object.entries({a: 1, b: 2}).length", "#num:2"},
      {"Object.entries({a: 1, b: 2})[0][0] + ':' + Object.entries({a: 1, b: 2})[0][1]",
       "#str:a:1"},
      /* 数组 keys：整数索引。 */
      {"Object.keys([10, 20, 30]).join(',')", "#str:0,1,2"},
      {"Object.values([10, 20, 30]).join(',')", "#str:10,20,30"},
      /* 字符串装箱 keys：索引。 */
      {"Object.keys('ab').join(',')", "#str:0,1"},
      {"Object.values('ab').join(',')", "#str:a,b"},
      /* 空对象。 */
      {"Object.keys({}).join(',')", "#str:"},
      /* Object.assign。 */
      {"var t = {}; Object.assign(t, {a: 1}, {b: 2}); t.a + t.b", "#num:3"},
      {"var t = {a: 1}; Object.assign(t, {a: 10}); t.a", "#num:10"},
      /* Object.defineProperty：enumerable 影响 keys。 */
      {"var o = {}; Object.defineProperty(o, 'x', {value: 42}); "
       "o.x + ':' + Object.keys(o).join(',')", "#str:42:x"},
      {"var o = {}; Object.defineProperty(o, 'x', {value: 42, enumerable: false}); "
       "o.x + ':' + Object.keys(o).length", "#str:42:0"},
      {"var o = {}; Object.defineProperty(o, 'x', {value: 1}); "
       "Object.defineProperty(o, 'y', {value: 2, enumerable: false}); "
       "Object.keys(o).join(',') + ':' + o.x + o.y", "#str:x:12"},
      /* Object.create + defineProperties。 */
      {"var o = Object.create(null, {a: {value: 5}, b: {value: 6}}); o.a + o.b", "#num:11"},
      /* Object.freeze / isFrozen。 */
      {"var o = {a: 1}; var f = Object.freeze(o); f === o && Object.isFrozen(o)", "#bool:1"},
      {"Object.isFrozen({a: 1})", "#bool:0"},
      /* Object.seal / isSealed。 */
      {"var o = {a: 1}; Object.seal(o); Object.isSealed(o) && !Object.isFrozen(o)",
       "#bool:1"},
      /* Object.preventExtensions / isExtensible。 */
      {"var o = {a: 1}; Object.preventExtensions(o); !Object.isExtensible(o)",
       "#bool:1"},
      {"Object.isExtensible({})", "#bool:1"},
      /* Object.fromEntries。 */
      {"var o = Object.fromEntries([['a', 1], ['b', 2]]); o.a + o.b", "#num:3"},
      {"Object.fromEntries([['x', 9]]).x", "#num:9"},
      /* Object.getPrototypeOf / setPrototypeOf（回归）。 */
      {"Object.getPrototypeOf({}) === Object.prototype", "#bool:1"},
      {"var p = {m: 1}; var o = Object.create(p); o.m", "#num:1"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " src=" << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'n') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), std::atof(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 's') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'b') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_BOOLEAN) {
          EXPECT_EQ(JS_ToBoolean(ctx, r), std::atoi(e + 6))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'e') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " src=" << cases[i].src << " got: "
            << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos)
              << "case " << i << " msg=" << msg;
        }
      }
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.4h：Object.prototype 方法补齐（§20.1.3）+ 访问器属性（defineProperty
 * get/set）+ writable 校验。expect 前缀同 EvalObjectStaticMethods。 */
TEST(Compiler, EvalObjectProtoMethods) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  struct { const char *src; const char *expect; } cases[] = {
      /* hasOwnProperty（§20.1.3.5）。对象字面量加括号（表达式语句以 { 开头
       * 会被解析为块语句）。 */
      {"({a: 1}).hasOwnProperty('a')", "#bool:1"},
      {"({a: 1}).hasOwnProperty('b')", "#bool:0"},
      {"({}).hasOwnProperty('toString')", "#bool:0"},
      {"Object.prototype.hasOwnProperty.call({a: 1}, 'a')", "#bool:1"},
      /* isPrototypeOf（§20.1.3.6）。 */
      {"Object.prototype.isPrototypeOf({})", "#bool:1"},
      {"Array.prototype.isPrototypeOf([])", "#bool:1"},
      {"Array.prototype.isPrototypeOf({})", "#bool:0"},
      /* propertyIsEnumerable（§20.1.3.7）。 */
      {"({a: 1}).propertyIsEnumerable('a')", "#bool:1"},
      {"Object.keys({a: 1}).length === 1 ? true : false", "#bool:1"},
      {"var o = {}; Object.defineProperty(o, 'x', {value: 1, enumerable: false}); "
       "o.propertyIsEnumerable('x')", "#bool:0"},
      /* toString（§20.1.3.7）。 */
      {"Object.prototype.toString.call({})", "#str:[object Object]"},
      {"Object.prototype.toString.call([])", "#str:[object Array]"},
      {"Object.prototype.toString.call(null)", "#str:[object Null]"},
      {"Object.prototype.toString.call(undefined)", "#str:[object Undefined]"},
      {"Object.prototype.toString.call(42)", "#str:[object Number]"},
      {"Object.prototype.toString.call('ab')", "#str:[object String]"},
      {"Object.prototype.toString.call(true)", "#str:[object Boolean]"},
      {"Object.prototype.toString.call(1n)", "#str:[object BigInt]"},
      {"Object.prototype.toString.call(Symbol('x'))", "#str:[object Symbol]"},
      /* Symbol.toStringTag 自定义 tag（§19.1.3.6 步骤 7）。 */
      {"var o = {}; o[Symbol.toStringTag] = 'Custom'; "
       "Object.prototype.toString.call(o)", "#str:[object Custom]"},
      {"var m = new Map(); Object.prototype.toString.call(m)", "#str:[object Map]"},
      /* valueOf / toLocaleString（§20.1.3.8/.6）。 */
      {"var o = {}; o.valueOf() === o", "#bool:1"},
      {"({a: 1}).toLocaleString()", "#str:[object Object]"},
      /* 访问器属性：defineProperty get/set。 */
      {"var o = {}; var v = 1; "
       "Object.defineProperty(o, 'x', {get: function() { return v; }, "
       "set: function(nv) { v = nv * 2; }}); "
       "o.x + ':' + (o.x = 5) + ':' + v", "#str:1:5:10"},
      {"var o = {}; Object.defineProperty(o, 'x', {get: function() { return 7; }}); "
       "o.x", "#num:7"},
      /* 只读访问器：无 setter 赋值 -> TypeError（§9.1.9 步骤 5）。 */
      {"var o = {}; Object.defineProperty(o, 'x', {get: function() { return 1; }}); "
       "o.x = 2;", "#err:TypeError"},
      /* writable=false 数据属性赋值 -> TypeError。 */
      {"var o = {}; Object.defineProperty(o, 'x', {value: 1, writable: false}); "
       "o.x = 2;", "#err:TypeError"},
      {"var o = {}; Object.defineProperty(o, 'x', {value: 1, writable: false}); "
       "o.x", "#num:1"},
      /* 继承访问器：子对象赋值走父 setter（§9.1.9 步骤 4）。 */
      {"var proto = {}; var v = 1; "
       "Object.defineProperty(proto, 'x', {get: function() { return v; }, "
       "set: function(nv) { v = nv; }}); "
       "var o = Object.create(proto); o.x = 9; o.x + ':' + v + ':' + "
       "o.hasOwnProperty('x')", "#str:9:9:false"},
      /* 访问器经 Object.values 取值走 getter（enumerable: true 显式）。
       * 副作用用 c = c + 1 计数（c++ 自增编译器未实现）。 */
      {"var o = {}; var c = 0; var inc = function() { c = c + 1; return 3; }; "
       "Object.defineProperty(o, 'x', {get: inc, enumerable: true}); "
       "Object.values(o)[0] + ':' + c", "#str:3:1"},
      /* freeze 对访问器属性：不置 writable（isFrozen 判定需区分）。 */
      {"var o = {}; var v = 1; "
       "Object.defineProperty(o, 'x', {get: function() { return v; }, configurable: true}); "
       "Object.freeze(o); Object.isFrozen(o)", "#bool:1"},
      /* 数组 writable 校验：length 不可写后赋值报错（回归现有语义）。 */
      {"var a = [1, 2]; Object.defineProperty(a, 'length', {writable: false}); "
       "a.length = 5;", "#err:TypeError"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " src=" << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'n') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          EXPECT_DOUBLE_EQ(JS_ToNumber(ctx, r), std::atof(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 's') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'b') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_BOOLEAN) {
          EXPECT_EQ(JS_ToBoolean(ctx, r), std::atoi(e + 6))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'e') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " src=" << cases[i].src << " got: "
            << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos)
              << "case " << i << " msg=" << msg;
        }
      }
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

/* M4.3f：String.prototype 方法补齐（§22.1.3）。
 * expect 前缀：#num: 数字；#str: 字符串；#bool: 布尔；#err: 异常子串；
 * #obj: 对象（split 返回数组）。 */
TEST(Compiler, EvalStringProtoMethods) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  JSValue *global = JS_GetGlobalObject(ctx);
  ASSERT_NE(global, nullptr);
  JS_FreeValue(ctx, global);

  struct { const char *src; const char *expect; } cases[] = {
      /* length 与索引访问（§10.4.3.3/.4）。 */
      {"'abc'.length", "#num:3"},
      {"'abc'[0]", "#str:a"},
      {"'abc'[2]", "#str:c"},
      {"'abc'[3]", "#undef"},
      {"'abc'[1.5]", "#undef"},  /* 非整数索引 -> 属性查找 */
      {"var s = new String('xy'); s.length", "#num:2"},
      {"var s = new String('xy'); s[1]", "#str:y"},
      /* at（§22.1.3.1）。 */
      {"'hello'.at(1)", "#str:e"},
      {"'hello'.at(-1)", "#str:o"},
      {"'hello'.at(99)", "#undef"},
      /* charAt（§22.1.3.2）。 */
      {"'hello'.charAt(1)", "#str:e"},
      {"'hello'.charAt(99)", "#str:"},
      {"'hello'.charAt(-1)", "#str:"},
      /* charCodeAt（§22.1.3.3）。 */
      {"'hello'.charCodeAt(0)", "#num:104"},
      {"'hello'.charCodeAt(99)", "#num:NaN"},
      /* codePointAt（§22.1.3.4）：增补平面字符。 */
      {"'\\u{1F600}'.codePointAt(0)", "#num:128512"},
      {"'\\u{1F600}'.codePointAt(1)", "#num:56832"},  /* 低代理 0xDE00 */
      {"'a'.codePointAt(99)", "#undef"},
      /* concat（§22.1.3.5）。 */
      {"'a'.concat('b', 'c')", "#str:abc"},
      {"'a'.concat()", "#str:a"},
      {"'a'.concat(1, true)", "#str:a1true"},
      /* includes（§22.1.3.8）。 */
      {"'abcdef'.includes('cde')", "#bool:1"},
      {"'abcdef'.includes('xyz')", "#bool:0"},
      {"'abcdef'.includes('cde', 3)", "#bool:0"},
      {"'abcdef'.includes()", "#bool:0"},
      /* indexOf（§22.1.3.9）。 */
      {"'hello'.indexOf('l')", "#num:2"},
      {"'hello'.indexOf('l', 3)", "#num:3"},
      {"'hello'.indexOf('x')", "#num:-1"},
      {"'hello'.indexOf()", "#num:-1"},
      /* lastIndexOf（§22.1.3.11）。 */
      {"'hello'.lastIndexOf('l')", "#num:3"},
      {"'hello'.lastIndexOf('l', 2)", "#num:2"},
      {"'hello'.lastIndexOf('x')", "#num:-1"},
      /* padStart/padEnd（§22.1.3.17/.16）。 */
      {"'5'.padStart(3, '0')", "#str:005"},
      {"'5'.padEnd(3, '0')", "#str:500"},
      {"'hello'.padStart(3)", "#str:hello"},
      {"'5'.padStart(5, 'ab')", "#str:abab5"},
      /* repeat（§22.1.3.18）。 */
      {"'ab'.repeat(3)", "#str:ababab"},
      {"'ab'.repeat(0)", "#str:"},
      {"'ab'.repeat(-1)", "#err:RangeError"},
      {"'ab'.repeat(Infinity)", "#err:RangeError"},
      /* replace（§22.1.3.19）：字符串替换 + $ 模式。 */
      {"'hello'.replace('l', 'L')", "#str:heLlo"},
      {"'hello'.replace('x', 'y')", "#str:hello"},
      {"'abc'.replace('b', '$&-$&')", "#str:ab-bc"},  /* $& = 匹配 'b' */
      {"'abc'.replace('b', '$$')", "#str:a$c"},
      /* slice（§22.1.3.22）。 */
      {"'hello'.slice(1, 3)", "#str:el"},
      {"'hello'.slice(-3)", "#str:llo"},
      {"'hello'.slice(-3, -1)", "#str:ll"},
      /* split（§22.1.3.23）。 */
      {"'a,b,c'.split(',')[1]", "#str:b"},
      {"'a,b,c'.split(',')[3]", "#undef"},
      {"'abc'.split('') [1]", "#str:b"},
      {"'abc'.split() [0]", "#str:abc"},
      {"'a,b,c'.split(',', 2).length", "#num:2"},
      /* startsWith / endsWith（§22.1.3.24/.7）。 */
      {"'hello'.startsWith('he')", "#bool:1"},
      {"'hello'.startsWith('lo')", "#bool:0"},
      {"'hello'.startsWith('lo', 3)", "#bool:1"},
      {"'hello'.endsWith('lo')", "#bool:1"},
      {"'hello'.endsWith('he')", "#bool:0"},
      {"'hello'.endsWith('he', 2)", "#bool:1"},
      /* substring（§22.1.3.25）：交换 + 夹取。 */
      {"'hello'.substring(1, 3)", "#str:el"},
      {"'hello'.substring(3, 1)", "#str:el"},
      {"'hello'.substring(-1, 3)", "#str:hel"},
      /* trim / trimStart / trimEnd（§22.1.3.32/.33/.34）。 */
      {"'  hi  '.trim()", "#str:hi"},
      {"'  hi  '.trimStart()", "#str:hi  "},
      {"'  hi  '.trimEnd()", "#str:  hi"},
      {"'\\t\\n hi \\t'.trim()", "#str:hi"},
      /* toLowerCase / toUpperCase（§22.1.3.28/.30）。 */
      {"'HeLLo'.toLowerCase()", "#str:hello"},
      {"'HeLLo'.toUpperCase()", "#str:HELLO"},
      {"'straße'.toUpperCase()", "#str:STRASSE"},
      /* 通用性：this 可转移（§22.1.3 前置）。 */
      {"String.prototype.indexOf.call('hello', 'l')", "#num:2"},
      {"String.prototype.toUpperCase.call(42)", "#str:42"},
      /* 无参调用缺参语义（undefined 串）。 */
      {"'abc'.concat(undefined)", "#str:abcundefined"},
      {"'abc'.indexOf(undefined)", "#num:-1"},
      {"'abc'.replace('a', undefined)", "#str:undefinedbc"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    JSValue *r = JS_Eval(ctx, cases[i].src, std::strlen(cases[i].src));
    ASSERT_NE(r, nullptr) << "case " << i << " src=" << cases[i].src;
    const char *e = cases[i].expect;
    if (e[0] == '#') {
      if (e[1] == 'e') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_EXCEPTION)
            << "case " << i << " src=" << cases[i].src << " got: "
            << JS_ToString(ctx, r);
        if (JS_GetKind(ctx, r) == JS_EXCEPTION) {
          std::string msg = JS_ToString(ctx, r);
          EXPECT_NE(msg.find(e + 5), std::string::npos)
              << "case " << i << " msg=" << msg;
        }
      } else if (e[1] == 'n') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_NUMBER)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_NUMBER) {
          double want = std::atof(e + 5);
          double got = JS_ToNumber(ctx, r);
          if (std::isnan(want)) {
            EXPECT_TRUE(std::isnan(got))
                << "case " << i << " src=" << cases[i].src << " got=" << got;
          } else {
            EXPECT_DOUBLE_EQ(got, want) << "case " << i << " src=" << cases[i].src;
          }
        }
      } else if (e[1] == 'u') { /* #undef */
        EXPECT_EQ(JS_GetKind(ctx, r), JS_UNDEFINED)
            << "case " << i << " src=" << cases[i].src << " got: "
            << JS_ToString(ctx, r);
      } else if (e[1] == 's') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_STRING) {
          EXPECT_EQ(std::string(JS_ToString(ctx, r)), std::string(e + 5))
              << "case " << i << " src=" << cases[i].src;
        }
      } else if (e[1] == 'b') {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_BOOLEAN)
            << "case " << i << " src=" << cases[i].src;
        if (JS_GetKind(ctx, r) == JS_BOOLEAN) {
          EXPECT_EQ(JS_ToBoolean(ctx, r), std::atoi(e + 6))
              << "case " << i << " src=" << cases[i].src;
        }
      } else {
        EXPECT_EQ(JS_GetKind(ctx, r), JS_OBJECT)
            << "case " << i << " src=" << cases[i].src;
      }
    } else {
      EXPECT_EQ(JS_GetKind(ctx, r), JS_STRING) << "case " << i;
    }
    JS_FreeValue(ctx, r);
  }

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}
