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

/* 编译错误：解构声明 -> not implemented */
TEST(Compiler, UnsupportedDestructuring) {
  JSAllocator *a = nullptr;
  JSContext *ctx = makeCtx(&a);
  ASSERT_NE(ctx, nullptr);

  neojs::DiagnosticSink sink;
  neojs::AST *ast =
      neojs::parseSource(&ic(ctx)->allocator(), "var [a] = [1]", 13, sink);
  ASSERT_NE(ast, nullptr);
  neojs::Compiler compiler;
  neojs::CompileResult cr = compiler.compile(*ast);
  ic(ctx)->allocator().destroy(ast);
  EXPECT_FALSE(cr.ok);
  EXPECT_NE(cr.error.find("destructuring"), std::string::npos);

  JS_FreeContext(ctx);
  JS_FreeAllocator(a);
}

