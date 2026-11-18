// AsyncCompilerTest.cpp - script async/await 端到端测试（ADR-045/060）
//
// 验证：
// - async function / async arrow 标记为 asyncScript，call 返回 Promise
// - await 产生 Interrupt(Await)，恢复时正常值压栈
// - await 拒绝以 Exception 注入，转为挂起点 throw，可被 try/catch 捕获
// - 未捕获拒绝传播为 Promise rejection

#include "compiler/Compiler.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "frontend/Parser.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "sema/Sema.h"
#include "vm/VM.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::compiler::Compiler;
using neo::compiler::CompilerOptions;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::detail::isErrorValue;
using neo::frontend::Parser;
using neo::frontend::ParserOptions;
using neo::frontend::Program;
using neo::frontend::SourceType;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::sema::SemaResult;
using neo::sema::runSema;
using neo::vm::VM;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    NullSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);
    std::unique_ptr<neo::vm::Code> code;
};

Value* g_captured = nullptr;

std::string errorText(IContext* ctx, Value* error) {
    if (error == nullptr || error->type()->kind() != TypeKind::Exception) {
        return "<not exception>";
    }
    auto* data = static_cast<neo::core::ExceptionData*>(error->data());
    Value* message = ctx->getProperty(data->child, ctx->createString("message"));
    if (message != nullptr && message->type()->kind() == TypeKind::String) {
        return static_cast<StringData*>(message->data())->value;
    }
    return "<no message>";
}

Value* capture(IContext* ctx, Value*, size_t argc, Value* argv[]) {
    g_captured = argc > 0 ? argv[0] : ctx->createUndefined();
    return ctx->createUndefined();
}

Value* runScript(Fixture& f, const std::string& source, const char* name) {
    Parser parser(source, ParserOptions{SourceType::Script});
    auto parsed = parser.parseProgram();
    EXPECT_TRUE(parsed.ok());
    SemaResult sema = runSema(*parsed.program);
    EXPECT_TRUE(sema.ok());

    Compiler compiler(static_cast<const Program&>(*parsed.program), sema,
                      CompilerOptions{"test.js"});
    auto result = compiler.compile();
    EXPECT_TRUE(result.ok());
    f.code = std::move(result.code);

    VM vm(f.ctx.get(), f.code.get());
    Value* res = vm.run();
    EXPECT_FALSE(isErrorValue(res)) << errorText(f.ctx.get(), res);
    return f.ctx->current()->lookupChain(name);
}

Value* get(IContext* ctx, Value* obj, const char* name) {
    return ctx->getProperty(obj, ctx->createString(name));
}

// 订阅 promise 并把结算值写入 g_captured，然后排空微任务。
void settle(IContext* ctx, Value* promise) {
    g_captured = nullptr;
    Value* thenFn = get(ctx, promise, "then");
    EXPECT_FALSE(isErrorValue(thenFn));
    Value* onFulfilled = ctx->createNativeFunction(capture, 1);
    Value* onRejected = ctx->createNativeFunction(capture, 1);
    Value* callbacks[2] = {onFulfilled, onRejected};
    Value* r = ctx->call(thenFn, promise, 2, callbacks);
    EXPECT_FALSE(isErrorValue(r));
    while (ctx->hasRunnableTask()) {
        Value* taskResult = ctx->runOneTask();
        EXPECT_FALSE(isErrorValue(taskResult));
    }
}

double number(Value* value) {
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Number);
    return static_cast<NumberData*>(value->data())->value;
}

std::string string(Value* value) {
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::String);
    return static_cast<StringData*>(value->data())->value;
}

}  // namespace

TEST(AsyncCompilerTest, AwaitResolvesAndReturns) {
    Fixture f;
    Value* promise = runScript(f,
                               "async function f() { var x = await 1; return x + 41; }\n"
                               "var p = f();",
                               "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_DOUBLE_EQ(number(g_captured), 42.0);
}

TEST(AsyncCompilerTest, AsyncArrowReturnsPromise) {
    Fixture f;
    Value* promise = runScript(f,
                               "function make() { return async () => 7; }\n"
                               "var p = make()();",
                               "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_DOUBLE_EQ(number(g_captured), 7.0);
}

TEST(AsyncCompilerTest, AwaitRejectionIsCatchable) {
    Fixture f;
    Value* promise = runScript(
        f,
        "async function f() {\n"
        "  try { await Promise.reject('boom'); }\n"
        "  catch (e) { return 'caught:' + e; }\n"
        "}\n"
        "var p = f();",
        "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_EQ(string(g_captured), "caught:boom");
}

TEST(AsyncCompilerTest, ForAwaitSyncIterable) {
    Fixture f;
    Value* promise = runScript(
        f,
        "async function f() {\n"
        "  var sum = 0;\n"
        "  for await (var x of [1, 2, 3]) sum += x;\n"
        "  return sum;\n"
        "}\n"
        "var p = f();",
        "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_DOUBLE_EQ(number(g_captured), 6.0);
}

TEST(AsyncCompilerTest, ForAwaitAsyncGenerator) {
    Fixture f;
    Value* promise = runScript(
        f,
        "async function* gen() { yield 1; yield 2; }\n"
        "async function f() {\n"
        "  var sum = 0;\n"
        "  for await (var x of gen()) sum += x;\n"
        "  return sum;\n"
        "}\n"
        "var p = f();",
        "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_DOUBLE_EQ(number(g_captured), 3.0);
}

TEST(AsyncCompilerTest, ForAwaitBreakClosesIterator) {
    Fixture f;
    Value* promise = runScript(
        f,
        "var closed = false;\n"
        "var iter = {\n"
        "  [Symbol.asyncIterator]() { return this; },\n"
        "  next() { return Promise.resolve({ value: 1, done: false }); },\n"
        "  return() { closed = true; return Promise.resolve({ done: true }); }\n"
        "};\n"
        "async function f() {\n"
        "  for await (var x of iter) break;\n"
        "  return closed;\n"
        "}\n"
        "var p = f();",
        "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    ASSERT_NE(g_captured, nullptr);
    ASSERT_EQ(g_captured->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(static_cast<neo::core::BooleanData*>(g_captured->data())->value);
}

TEST(AsyncCompilerTest, ForAwaitDestructuring) {
    Fixture f;
    Value* promise = runScript(
        f,
        "async function f() {\n"
        "  var s = '';\n"
        "  for await (var [a, b] of [[1, 2], [3, 4]]) s += a + b;\n"
        "  return s;\n"
        "}\n"
        "var p = f();",
        "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_EQ(string(g_captured), "37");
}

TEST(AsyncCompilerTest, UncaughtRejectionPropagates) {
    Fixture f;
    Value* promise = runScript(f,
                               "async function f() { await Promise.reject('bad'); }\n"
                               "var p = f();",
                               "p");
    ASSERT_FALSE(isErrorValue(promise));
    settle(f.ctx.get(), promise);
    EXPECT_EQ(string(g_captured), "bad");
}
