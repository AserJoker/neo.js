// GeneratorCompilerTest.cpp - script 生成器挂起/恢复端到端测试（ADR-045/060）
//
// 验证：
// - function* 标记为生成器，call 返回 Suspended 协程而非执行函数体
// - yield 产生 Interrupt(Yield)，VmState 保存 pc/操作数栈/label/try 栈
// - next(v) 恢复并把 v 作为 yield 表达式结果
// - throw/return 注入在挂起点展开，finally 正常参与
// - 跨 yield 的块作用域与循环状态保持

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
    // script 函数借用 Code，必须活到生成器结束（真实运行由 Module 持有）。
    std::unique_ptr<neo::vm::Code> code;
};

// 编译并执行一段 script，返回顶层绑定 name 的值。
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
    EXPECT_FALSE(isErrorValue(res));
    return f.ctx->current()->lookupChain(name);
}

Value* get(IContext* ctx, Value* obj, const char* name) {
    return ctx->getProperty(obj, ctx->createString(name));
}

Value* callMethod(IContext* ctx, Value* obj, const char* name, size_t argc,
                  Value* argv[]) {
    Value* fn = get(ctx, obj, name);
    EXPECT_FALSE(isErrorValue(fn));
    return ctx->call(fn, obj, argc, argv);
}

double number(Value* value) {
    EXPECT_NE(value, nullptr);
    EXPECT_EQ(value->type()->kind(), TypeKind::Number);
    return static_cast<NumberData*>(value->data())->value;
}

bool boolean(IContext* ctx, Value* obj, const char* name) {
    Value* value = get(ctx, obj, name);
    return value != nullptr && value->type()->kind() == TypeKind::Boolean &&
           static_cast<neo::core::BooleanData*>(value->data())->value;
}

std::string errorText(IContext* ctx, Value* error) {
    if (error == nullptr || error->type()->kind() != TypeKind::Exception) {
        return "<not exception>";
    }
    auto* data = static_cast<neo::core::ExceptionData*>(error->data());
    Value* message = ctx->getProperty(data->child, ctx->createString("message"));
    if (message != nullptr && message->type()->kind() == TypeKind::String) {
        return static_cast<neo::core::StringData*>(message->data())->value;
    }
    return "<no message>";
}

}  // namespace

TEST(GeneratorCompilerTest, YieldsThenReturns) {
    Fixture f;
    Value* g = runScript(f,
                         "function* gen() { yield 1; yield 2; return 3; }\n"
                         "var g = gen();",
                         "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1)) << errorText(f.ctx.get(), r1);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 1.0);
    EXPECT_FALSE(boolean(f.ctx.get(), r1, "done"));

    Value* r2 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r2));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 2.0);
    EXPECT_FALSE(boolean(f.ctx.get(), r2, "done"));

    Value* r3 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r3));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r3, "value")), 3.0);
    EXPECT_TRUE(boolean(f.ctx.get(), r3, "done"));

    Value* r4 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r4));
    EXPECT_TRUE(boolean(f.ctx.get(), r4, "done"));
}

TEST(GeneratorCompilerTest, NextValueBecomesYieldResult) {
    Fixture f;
    Value* g = runScript(f,
                         "function* gen() { var x = yield 1; return x + 10; }\n"
                         "var g = gen();",
                         "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 1.0);

    Value* sent = f.ctx->createNumber(5);
    Value* r2 = callMethod(f.ctx.get(), g, "next", 1, &sent);
    ASSERT_FALSE(isErrorValue(r2));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 15.0);
    EXPECT_TRUE(boolean(f.ctx.get(), r2, "done"));
}

TEST(GeneratorCompilerTest, LoopStateSurvivesYield) {
    Fixture f;
    Value* g = runScript(
        f,
        "function* gen() { var i = 0; while (i < 2) { yield i; i = i + 1; } }\n"
        "var g = gen();",
        "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 0.0);

    Value* r2 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r2));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 1.0);

    Value* r3 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r3));
    EXPECT_TRUE(boolean(f.ctx.get(), r3, "done"));
}

TEST(GeneratorCompilerTest, ThrowIsInjectedAtYield) {
    Fixture f;
    Value* g = runScript(
        f,
        "function* gen() { try { yield 1; } catch (e) { yield 2; } }\n"
        "var g = gen();",
        "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 1.0);

    Value* error = f.ctx->createString("boom");
    Value* r2 = callMethod(f.ctx.get(), g, "throw", 1, &error);
    ASSERT_FALSE(isErrorValue(r2)) << errorText(f.ctx.get(), r2);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 2.0);
    EXPECT_FALSE(boolean(f.ctx.get(), r2, "done"));

    Value* r3 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r3));
    EXPECT_TRUE(boolean(f.ctx.get(), r3, "done"));
}

TEST(GeneratorCompilerTest, ReturnInjectionCompletes) {
    Fixture f;
    Value* g = runScript(f,
                         "function* gen() { yield 1; yield 2; }\n"
                         "var g = gen();",
                         "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 1.0);

    Value* sent = f.ctx->createNumber(9);
    Value* r2 = callMethod(f.ctx.get(), g, "return", 1, &sent);
    ASSERT_FALSE(isErrorValue(r2));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 9.0);
    EXPECT_TRUE(boolean(f.ctx.get(), r2, "done"));
}

TEST(GeneratorCompilerTest, FinallyRunsAcrossYield) {
    Fixture f;
    Value* g = runScript(
        f,
        "function* gen() { try { yield 1; } finally { yield 2; } }\n"
        "var g = gen();",
        "g");

    Value* r1 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r1));
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r1, "value")), 1.0);

    Value* r2 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r2)) << errorText(f.ctx.get(), r2);
    EXPECT_DOUBLE_EQ(number(get(f.ctx.get(), r2, "value")), 2.0);
    EXPECT_FALSE(boolean(f.ctx.get(), r2, "done"));

    Value* r3 = callMethod(f.ctx.get(), g, "next", 0, nullptr);
    ASSERT_FALSE(isErrorValue(r3));
    EXPECT_TRUE(boolean(f.ctx.get(), r3, "done"));
}
