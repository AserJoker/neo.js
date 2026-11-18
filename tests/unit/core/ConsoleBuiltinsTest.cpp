// ConsoleBuiltinsTest.cpp - console 与 queueMicrotask
//
// 验证：
// - console.log/info/warn/error/debug 以空格连接参数并输出一行
// - console.assert 仅在条件为假时输出
// - queueMicrotask 入 microtask 队列，runOneTask 执行

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::IContext;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class RecordingSink : public IOutputSink {
public:
    std::vector<std::string> lines;
    void write(std::string_view text) override { lines.emplace_back(text); }
    void writeLine(std::string_view text) override { lines.emplace_back(text); }
    void flush() override {}
};

struct Fixture {
    std::unique_ptr<neo::infra::IAllocator> alloc = createMallocAllocator();
    RecordingSink sink;
    Console console{&sink};
    std::unique_ptr<IContext> ctx = createContext(alloc.get(), &console);

    Value* consoleObject() { return ctx->global()->lookup("console"); }

    Value* call(Value* self, const char* name, size_t argc = 0, Value* argv[] = nullptr) {
        Value* fn = ctx->getProperty(self, name);
        return ctx->call(fn, self, argc, argv);
    }
};

bool isError(Value* v) {
    return v != nullptr && (v->type()->kind() == TypeKind::Exception ||
                            v->type()->kind() == TypeKind::Fatal);
}

int g_microtaskRuns = 0;

Value* countMicrotask(IContext* ctx, Value*, size_t, Value*[]) {
    ++g_microtaskRuns;
    return ctx->createUndefined();
}

}  // namespace

TEST(ConsoleBuiltinsTest, LogJoinsArguments) {
    Fixture f;
    Value* args[3] = {f.ctx->createString("a"), f.ctx->createNumber(1.0),
                      f.ctx->createBoolean(true)};
    Value* r = f.call(f.consoleObject(), "log", 3, args);
    ASSERT_FALSE(isError(r));
    ASSERT_EQ(f.sink.lines.size(), 1u);
    EXPECT_EQ(f.sink.lines[0], "a 1 true");

    Value* none = f.call(f.consoleObject(), "error");
    ASSERT_FALSE(isError(none));
    ASSERT_EQ(f.sink.lines.size(), 2u);
    EXPECT_EQ(f.sink.lines[1], "");
}

TEST(ConsoleBuiltinsTest, AssertOnlyOnFailure) {
    Fixture f;
    Value* ok[2] = {f.ctx->createBoolean(true), f.ctx->createString("hidden")};
    f.call(f.consoleObject(), "assert", 2, ok);
    EXPECT_TRUE(f.sink.lines.empty());

    Value* bad[2] = {f.ctx->createBoolean(false), f.ctx->createString("boom")};
    f.call(f.consoleObject(), "assert", 2, bad);
    ASSERT_EQ(f.sink.lines.size(), 1u);
    EXPECT_EQ(f.sink.lines[0], "Assertion failed: boom");
}

TEST(ConsoleBuiltinsTest, QueueMicrotask) {
    Fixture f;
    g_microtaskRuns = 0;
    Value* fn = f.ctx->global()->lookup("queueMicrotask");
    Value* cb = f.ctx->createNativeFunction(countMicrotask, 0);
    Value* args[1] = {cb};
    ASSERT_FALSE(isError(f.ctx->call(fn, f.ctx->createUndefined(), 1, args)));
    EXPECT_EQ(g_microtaskRuns, 0);
    f.ctx->runOneTask();
    EXPECT_EQ(g_microtaskRuns, 1);

    Value* bad[1] = {f.ctx->createNumber(1.0)};
    EXPECT_TRUE(isError(f.ctx->call(fn, f.ctx->createUndefined(), 1, bad)));
}
