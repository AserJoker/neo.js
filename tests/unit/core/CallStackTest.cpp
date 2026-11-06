// CallStackTest.cpp - 调用栈（纯展示信息）单元测试
//
// 验证（ADR-037）：
// - 帧语义「在某个函数的某处」：push 跨两帧（调用点写入调用者帧 + 压入新帧）
// - 位置用 SourcePosition（filename/line/column 三字段），非源码帧为 "<internal>"
// - trace：clone 一份并用入参补全栈顶帧，不改动 Context 的栈
// - popFrame 弹栈（栈空时无操作）；setCallStack 整体替换
// - 调用栈与 Scope 链互不影响（纯展示信息）

#include "core/CallStack.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using neo::core::CallFrame;
using neo::core::IContext;
using neo::core::SourcePosition;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

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
};

SourcePosition at(const char* file, uint32_t line, uint32_t column) {
    return SourcePosition(file, line, column);
}

}  // namespace

TEST(CallStackTest, InitiallyEmpty) {
    Fixture f;
    EXPECT_TRUE(f.ctx->callStack().empty());
}

TEST(CallStackTest, FirstPushOnlyEntersFrame) {
    Fixture f;
    // 最外层调用：没有调用者，调用点无处可写，只压入新帧
    f.ctx->pushFrame(at("a.js", 1, 1), "main");

    const std::vector<CallFrame>& stack = f.ctx->callStack();
    ASSERT_EQ(stack.size(), 1u);
    EXPECT_EQ(stack[0].name, "main");
    EXPECT_TRUE(stack[0].position.filename.empty());  // 位置待定
}

TEST(CallStackTest, PushWritesCallSiteIntoCallerFrame) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "main");
    // main 在 a.js:10:5 调用 foo
    f.ctx->pushFrame(at("a.js", 10, 5), "foo");

    const std::vector<CallFrame>& stack = f.ctx->callStack();
    ASSERT_EQ(stack.size(), 2u);
    // 调用者帧补上了调用点
    EXPECT_EQ(stack[0].name, "main");
    EXPECT_EQ(stack[0].position.filename, "a.js");
    EXPECT_EQ(stack[0].position.line, 10u);
    EXPECT_EQ(stack[0].position.column, 5u);
    // 新帧只有名字
    EXPECT_EQ(stack[1].name, "foo");
    EXPECT_TRUE(stack[1].position.filename.empty());
}

TEST(CallStackTest, NestedPushesRecordEachCallSite) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "main");
    f.ctx->pushFrame(at("a.js", 10, 5), "foo");
    f.ctx->pushFrame(at("a.js", 20, 9), "bar");

    const std::vector<CallFrame>& stack = f.ctx->callStack();
    ASSERT_EQ(stack.size(), 3u);
    EXPECT_EQ(stack[0].name, "main");
    EXPECT_EQ(stack[0].position.line, 10u);  // main 调用 foo 的位置
    EXPECT_EQ(stack[1].name, "foo");
    EXPECT_EQ(stack[1].position.line, 20u);  // foo 调用 bar 的位置
    EXPECT_EQ(stack[2].name, "bar");
    EXPECT_TRUE(stack[2].position.filename.empty());  // 栈顶位置未知
}

TEST(CallStackTest, TraceCompletesTopFrameWithoutMutating) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "main");
    f.ctx->pushFrame(at("a.js", 10, 5), "foo");

    // 出错点在 foo 内的 a.js:12:3
    std::vector<CallFrame> snapshot = f.ctx->trace(at("a.js", 12, 3));
    ASSERT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(snapshot[0].name, "main");
    EXPECT_EQ(snapshot[0].position.line, 10u);
    EXPECT_EQ(snapshot[1].name, "foo");
    EXPECT_EQ(snapshot[1].position.filename, "a.js");
    EXPECT_EQ(snapshot[1].position.line, 12u);
    EXPECT_EQ(snapshot[1].position.column, 3u);

    // Context 的栈未被改动
    EXPECT_TRUE(f.ctx->callStack()[1].position.filename.empty());
}

TEST(CallStackTest, TraceOnEmptyStackReturnsEmpty) {
    Fixture f;
    EXPECT_TRUE(f.ctx->trace(at("a.js", 1, 1)).empty());
}

TEST(CallStackTest, PopRemovesTopFrame) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "main");
    f.ctx->pushFrame(at("a.js", 10, 5), "foo");

    f.ctx->popFrame();
    ASSERT_EQ(f.ctx->callStack().size(), 1u);
    EXPECT_EQ(f.ctx->callStack()[0].name, "main");
    EXPECT_EQ(f.ctx->callStack()[0].position.line, 10u);  // 调用点保留

    f.ctx->popFrame();
    EXPECT_TRUE(f.ctx->callStack().empty());
}

TEST(CallStackTest, PopOnEmptyIsNoop) {
    Fixture f;
    f.ctx->popFrame();
    EXPECT_TRUE(f.ctx->callStack().empty());
}

TEST(CallStackTest, InternalFrameUsesMarker) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "main");
    // native 内建：位置用 <internal>
    f.ctx->pushFrame(SourcePosition::internal(), "Array.prototype.map");

    const std::vector<CallFrame>& stack = f.ctx->callStack();
    ASSERT_EQ(stack.size(), 2u);
    EXPECT_TRUE(stack[0].position.isInternal());  // 调用者位置是 native 调用点
    EXPECT_EQ(stack[0].position.filename, "<internal>");
    EXPECT_EQ(stack[0].position.line, 0u);
    EXPECT_EQ(stack[1].name, "Array.prototype.map");
    EXPECT_TRUE(stack[1].position.filename.empty());

    // 也可直接构造带 <internal> 的帧（如 setCallStack 恢复快照时）
    std::vector<CallFrame> saved{CallFrame::internal("Promise.then")};
    f.ctx->setCallStack(saved);
    EXPECT_TRUE(f.ctx->callStack()[0].position.isInternal());
    EXPECT_EQ(f.ctx->callStack()[0].position.filename, neo::core::kInternalSource);
}

TEST(CallStackTest, SetCallStackReplacesWhole) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "old");

    // 异常/异步/生成器恢复时整体写回自己那份栈
    std::vector<CallFrame> saved;
    saved.emplace_back("savedFn", at("b.js", 9, 3));
    saved.push_back(CallFrame::internal("Promise.then"));
    f.ctx->setCallStack(saved);

    const std::vector<CallFrame>& stack = f.ctx->callStack();
    ASSERT_EQ(stack.size(), 2u);
    EXPECT_EQ(stack[0].name, "savedFn");
    EXPECT_EQ(stack[0].position.filename, "b.js");
    EXPECT_EQ(stack[0].position.line, 9u);
    EXPECT_TRUE(stack[1].position.isInternal());

    // 取快照后修改 Context 的栈，不影响已取出的副本
    std::vector<CallFrame> snapshot = f.ctx->callStack();
    f.ctx->popFrame();
    EXPECT_EQ(snapshot.size(), 2u);
    EXPECT_EQ(f.ctx->callStack().size(), 1u);
}

TEST(CallStackTest, SetEmptyClears) {
    Fixture f;
    f.ctx->pushFrame(at("a.js", 1, 1), "foo");
    f.ctx->setCallStack({});
    EXPECT_TRUE(f.ctx->callStack().empty());
}

TEST(CallStackTest, IndependentOfScopeChain) {
    Fixture f;
    // push scope 不产生栈帧
    f.ctx->pushNewScope(f.ctx->current());
    EXPECT_TRUE(f.ctx->callStack().empty());
    // push 帧不改变 current scope
    neo::core::Scope* before = f.ctx->current();
    f.ctx->pushFrame(at("a.js", 1, 1), "foo");
    EXPECT_EQ(f.ctx->current(), before);
    f.ctx->popFrame();
    f.ctx->popScope();
}
