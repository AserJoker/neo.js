// ErrorKindTest.cpp - 引擎内部报错的规范错误类型映射（ADR-039）
//
// 验证：
// - createErrorException 按 kind 创建对应 Error 对象并包进 Exception 通道
// - 各错误类型名映射
// - 现有实现整改后的实际错误类型：
//     数组长度越界 → RangeError
//     非函数调用 / 非构造函数 / 只读属性 / 原型非法 → TypeError
//     未定义标识符 / 严格模式未声明赋值 → ReferenceError
//     重复声明 → SyntaxError
// - Error 对象的 message / stack 正确

#include "core/ErrorKind.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Exception.h"
#include "core/types/TypeBehavior.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::ErrorKind;
using neo::core::IContext;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
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

std::string str(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

// 取 Exception 携带的 Error 对象
Value* errorOf(Value* exception) {
    return static_cast<neo::core::ExceptionData*>(exception->data())->child;
}

// 断言 exception 是携带指定类型 Error 的 Exception
void expectError(IContext* ctx, Value* exception, const char* name) {
    ASSERT_NE(exception, nullptr);
    ASSERT_EQ(exception->type()->kind(), TypeKind::Exception);
    Value* err = errorOf(exception);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(ctx->getProperty(err, "name")), name);
}

}  // namespace

// ===== 基础 =====

TEST(ErrorKindTest, KindNames) {
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::Error), "Error");
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::TypeError), "TypeError");
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::RangeError), "RangeError");
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::ReferenceError), "ReferenceError");
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::SyntaxError), "SyntaxError");
    EXPECT_STREQ(neo::core::errorKindName(ErrorKind::URIError), "URIError");
}

TEST(ErrorKindTest, CreateErrorExceptionWrapsErrorObject) {
    Fixture f;
    Value* e = f.ctx->createErrorException(ErrorKind::TypeError, "boom");
    ASSERT_EQ(e->type()->kind(), TypeKind::Exception);
    Value* err = errorOf(e);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "boom");
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "TypeError: boom");
    // 原型是该子类原型
    EXPECT_EQ(f.ctx->getPrototype(err),
              f.ctx->getProperty(f.ctx->global()->lookup("TypeError"), "prototype"));
}

TEST(ErrorKindTest, CreateBaseError) {
    Fixture f;
    Value* e = f.ctx->createErrorException(ErrorKind::Error, "boom");
    Value* err = errorOf(e);
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "Error");
    EXPECT_EQ(f.ctx->getPrototype(err), f.ctx->errorPrototype());
}

TEST(ErrorKindTest, AllKindsHaveDistinctPrototypes) {
    Fixture f;
    Value* prev = nullptr;
    for (ErrorKind k : {ErrorKind::Error, ErrorKind::TypeError, ErrorKind::RangeError,
                        ErrorKind::ReferenceError, ErrorKind::SyntaxError,
                        ErrorKind::URIError}) {
        Value* err = errorOf(f.ctx->createErrorException(k, "x"));
        Value* proto = f.ctx->getPrototype(err);
        EXPECT_NE(proto, prev) << neo::core::errorKindName(k);
        prev = proto;
    }
}

// ===== 现有实现整改：数组 =====

TEST(ErrorKindTest, ArrayInvalidLengthIsRangeError) {
    Fixture f;
    Value* arrayFn = f.ctx->global()->lookup("Array");
    Value* args[] = {f.ctx->createNumber(-1)};
    expectError(f.ctx.get(), f.ctx->construct(arrayFn, 1, args), "RangeError");

    Value* frac[] = {f.ctx->createNumber(1.5)};
    expectError(f.ctx.get(), f.ctx->construct(arrayFn, 1, frac), "RangeError");

    Value* big[] = {f.ctx->createNumber(4294967296.0)};
    expectError(f.ctx.get(), f.ctx->construct(arrayFn, 1, big), "RangeError");
}

TEST(ErrorKindTest, ArrayDeleteLengthIsTypeError) {
    Fixture f;
    Value* arr = f.ctx->createArray();
    expectError(f.ctx.get(), f.ctx->deleteProperty(arr, "length"), "TypeError");
}

TEST(ErrorKindTest, ArrayMethodOnNonArrayIsTypeError) {
    Fixture f;
    Value* push = f.ctx->getProperty(f.ctx->arrayPrototype(), "push");
    Value* r = f.ctx->call(push, f.ctx->createNumber(1), 0, nullptr);
    expectError(f.ctx.get(), r, "TypeError");
}

// ===== 现有实现整改：类型/调用 =====

TEST(ErrorKindTest, CallingNonFunctionIsTypeError) {
    Fixture f;
    Value* r = f.ctx->call(f.ctx->createNumber(1), nullptr, 0, nullptr);
    expectError(f.ctx.get(), r, "TypeError");
}

TEST(ErrorKindTest, ConstructingNonConstructorIsTypeError) {
    Fixture f;
    Value* r = f.ctx->construct(f.ctx->createObject(), 0, nullptr);
    expectError(f.ctx.get(), r, "TypeError");
}

TEST(ErrorKindTest, AssignToReadOnlyIsTypeError) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "x", f.ctx->createNumber(1), false, true, true);
    Value* r = f.ctx->setProperty(obj, "x", f.ctx->createNumber(2));
    expectError(f.ctx.get(), r, "TypeError");
}

TEST(ErrorKindTest, InvalidPrototypeIsTypeError) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* r = f.ctx->setPrototype(obj, f.ctx->createNumber(1));
    expectError(f.ctx.get(), r, "TypeError");
}

TEST(ErrorKindTest, SymbolConversionIsTypeError) {
    Fixture f;
    Value* sym = f.ctx->createSymbol("s");
    // 显式 toString 合法（"Symbol(s)"）；隐式 ToString/ToNumber 才抛 TypeError
    EXPECT_EQ(str(f.ctx->toString(sym)), "Symbol(s)");
    expectError(f.ctx.get(), f.ctx->add(sym, f.ctx->createNumber(1)), "TypeError");
    expectError(f.ctx.get(), f.ctx->sub(sym, f.ctx->createNumber(1)), "TypeError");
}

TEST(ErrorKindTest, ToPrimitiveFailureIsTypeError) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    // 无 valueOf/toString 可用 → TypeError
    f.ctx->setPrototype(obj, f.ctx->createNull());
    expectError(f.ctx.get(), f.ctx->toString(obj), "TypeError");
}

// ===== 现有实现整改：作用域 =====

TEST(ErrorKindTest, RedeclarationIsSyntaxError) {
    Fixture f;
    Value* v = f.ctx->createNumber(1);
    ASSERT_EQ(f.ctx->define("x", v)->type()->kind(), TypeKind::Number);
    expectError(f.ctx.get(), f.ctx->define("x", v), "SyntaxError");
}

TEST(ErrorKindTest, AssignToConstIsTypeError) {
    Fixture f;
    Value* v = f.ctx->createNumber(1);
    Value* slot = f.ctx->define("c", v);
    f.ctx->setMutable(slot, false);
    expectError(f.ctx.get(), f.ctx->store("c", f.ctx->createNumber(2)), "TypeError");
}

TEST(ErrorKindTest, StrictUndeclaredAssignmentIsReferenceError) {
    Fixture f;
    // global 默认标注 strict
    expectError(f.ctx.get(), f.ctx->store("undeclared", f.ctx->createNumber(1)),
                "ReferenceError");
}

TEST(ErrorKindTest, MissingClosureVariableIsReferenceError) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    expectError(f.ctx.get(), f.ctx->setClosure(fn, "nope"), "ReferenceError");
}

// ===== 现有实现整改：内建 =====

TEST(ErrorKindTest, IteratorResultNotObjectIsTypeError) {
    Fixture f;
    Value* obj = f.ctx->createObject();
    Value* next = f.ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    f.ctx->defineProperty(obj, "next", next);
    expectError(f.ctx.get(), neo::core::detail::iteratorNext(f.ctx.get(), obj),
                "TypeError");
}

TEST(ErrorKindTest, TimerCallbackNotFunctionIsTypeError) {
    Fixture f;
    Value* setTimeout = f.ctx->global()->lookup("setTimeout");
    Value* args[] = {f.ctx->createNumber(1)};
    expectError(f.ctx.get(), f.ctx->call(setTimeout, nullptr, 1, args), "TypeError");
}

TEST(ErrorKindTest, ObjectKeysOnNullOrUndefinedIsTypeError) {
    Fixture f;
    Value* keys = f.ctx->getProperty(f.ctx->global()->lookup("Object"), "keys");
    Value* args[] = {f.ctx->createUndefined()};
    expectError(f.ctx.get(), f.ctx->call(keys, nullptr, 1, args), "TypeError");
}
