// VMTest.cpp - V0 VM → Engine 纵向链路测试（ADR-060）
//
// 直接手工构造 Code，不依赖 Compiler，验证：
// - 操作数栈与算术（1 + 2）
// - 字符串/数值常量表下标寻址
// - define / initialize / load / store 绑定语义（含 TDZ 与 const）
// - ret / hlt 返回协议
// - SourceMap 源码→字节码反查与异常栈位置补全

#include "core/IContext.h"
#include "core/Scope.h"
#include "core/types/Exception.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"
#include "vm/Code.h"
#include "vm/VM.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::ExceptionData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::SourcePosition;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;
using neo::vm::Code;
using neo::vm::OpCode;
using neo::vm::SourceMap;
using neo::vm::VM;
using neo::vm::kDefineInitialized;
using neo::vm::kDefineMutable;

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

double numberValue(Value* value) {
    return static_cast<NumberData*>(value->data())->value;
}

std::string stringValue(Value* value) {
    return static_cast<StringData*>(value->data())->value;
}

// Exception 载荷（Error 对象）的属性读取。
Value* errorChild(Value* exception) {
    return static_cast<ExceptionData*>(exception->data())->child;
}

std::string propertyString(IContext* ctx, Value* object, const char* name) {
    Value* value = ctx->getProperty(object, name);
    if (value == nullptr || value->type()->kind() != TypeKind::String) return {};
    return stringValue(value);
}

}  // namespace

TEST(VMTest, AddNumbers) {
    Fixture f;
    Code code;
    code.push(1.0);
    code.push(2.0);
    code.add();
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(result), 3.0);
}

TEST(VMTest, RunStartsAtEntryPc) {
    Fixture f;
    Code code;
    code.halt();       // pc 0：模块顶层占位
    code.push(42.0);   // pc 1：函数入口（entryPc）
    code.ret();        // pc 2

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run(1);

    ASSERT_EQ(result->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numberValue(result), 42.0);
}

TEST(VMTest, EntryPcOutOfRangeFails) {
    Fixture f;
    Code code;
    code.halt();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run(2);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Exception);
}

TEST(VMTest, PushStringUsesStringTable) {
    Fixture f;
    Code code;
    code.push("hello");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_EQ(result->type()->kind(), TypeKind::String);
    EXPECT_EQ(stringValue(result), "hello");
}

TEST(VMTest, DefineAndLoad) {
    Fixture f;
    Code code;
    code.push(42.0);
    code.define("x");
    code.load("x");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    EXPECT_DOUBLE_EQ(numberValue(result), 42.0);
}

TEST(VMTest, StoreUpdatesBinding) {
    Fixture f;
    Code code;
    code.push(1.0);
    code.define("x");
    code.push(2.0);
    code.store("x");
    code.load("x");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    EXPECT_DOUBLE_EQ(numberValue(result), 2.0);
}

TEST(VMTest, InitializeReleasesTdz) {
    Fixture f;
    Code code;
    code.pushUndefined();
    // 提升建槽：mutable 但未初始化（TDZ）
    code.define("x", true, false);
    code.push(7.0);
    code.initialize("x");
    code.load("x");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    EXPECT_DOUBLE_EQ(numberValue(result), 7.0);
}

TEST(VMTest, LoadBeforeInitializeFailsWithTdz) {
    Fixture f;
    Code code;
    code.pushUndefined();
    code.define("x", true, false);
    code.load("x");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_EQ(result->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(propertyString(f.ctx.get(), errorChild(result), "name"),
              "ReferenceError");
}

TEST(VMTest, StoreToConstFails) {
    Fixture f;
    Code code;
    code.push(1.0);
    // const：不可变且已初始化
    code.define("x", false, true);
    code.push(2.0);
    code.store("x");
    code.ret();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_EQ(result->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(propertyString(f.ctx.get(), errorChild(result), "name"),
              "TypeError");
}

TEST(VMTest, HaltReturnsUndefined) {
    Fixture f;
    Code code;
    code.halt();

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->type()->kind(), TypeKind::Undefined);
}

TEST(VMTest, BuilderEmitsSemanticInstructions) {
    Code code;
    EXPECT_EQ(code.here(), 0u);
    EXPECT_EQ(code.push(1.5), 0u);
    EXPECT_EQ(code.push("x"), 1u);
    EXPECT_EQ(code.define("v"), 2u);
    EXPECT_EQ(code.load("v"), 3u);
    EXPECT_EQ(code.ret(), 4u);
    EXPECT_EQ(code.here(), 5u);

    ASSERT_EQ(code.numbers.size(), 1u);
    EXPECT_DOUBLE_EQ(code.numbers[0], 1.5);
    // 字符串表不做去重：push "x"、define "v"、load "v" 各登记一次。
    ASSERT_EQ(code.strings.size(), 3u);
    EXPECT_EQ(code.strings[0], "x");
    EXPECT_EQ(code.strings[1], "v");
    EXPECT_EQ(code.strings[2], "v");

    ASSERT_EQ(code.instructions.size(), 5u);
    EXPECT_EQ(code.instructions[0].op, OpCode::PushNumber);
    EXPECT_EQ(code.instructions[0].a, 0u);
    EXPECT_EQ(code.instructions[1].op, OpCode::PushString);
    EXPECT_EQ(code.instructions[1].a, 0u);
    EXPECT_EQ(code.instructions[2].op, OpCode::Define);
    EXPECT_EQ(code.instructions[2].a, 1u);
    EXPECT_EQ(code.instructions[2].b, kDefineMutable | kDefineInitialized);
}

TEST(VMTest, SourceMapLookupPrefersInnermostRange) {
    SourceMap map;
    map.add(0, 10, SourcePosition("file.js", 1, 1));
    map.add(2, 5, SourcePosition("file.js", 2, 3));
    map.add(3, 4, SourcePosition("file.js", 3, 7));

    ASSERT_EQ(map.lookup(3)->line, 3u);
    EXPECT_EQ(map.lookup(3)->column, 7u);
    ASSERT_EQ(map.lookup(2)->line, 2u);
    EXPECT_EQ(map.lookup(2)->column, 3u);
    ASSERT_EQ(map.lookup(6)->line, 1u);
    EXPECT_EQ(map.lookup(10), nullptr);
}

TEST(VMTest, FailureAnnotatesExceptionStackWithSourcePosition) {
    Fixture f;
    Code code;
    uint32_t loadPc = code.load("missing");
    code.ret();
    code.mapSource(loadPc, loadPc + 1, SourcePosition("entry.js", 4, 9));

    // 调用栈由调用点维护（ADR-037）：先压入本次执行的帧，出错位置才能落到栈顶。
    f.ctx->pushFrame(SourcePosition{}, "top");

    VM vm(f.ctx.get(), &code);
    Value* result = vm.run();

    ASSERT_EQ(result->type()->kind(), TypeKind::Exception);
    std::string stack = propertyString(f.ctx.get(), errorChild(result), "stack");
    EXPECT_NE(stack.find("entry.js:4:9"), std::string::npos) << stack;
}
