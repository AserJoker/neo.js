// ErrorTest.cpp - Error 基类单元测试
//
// 验证（ADR-038）：
// - Error 是 [[ErrorData]] 对象：TypeKind::Error、具备对象布局、原型 = %Error.prototype%
// - Error(message) / new Error(message)：message 为 undefined 时不建属性
// - message / stack 是 writable / 不可枚举 / 可配置的自有属性
// - stack 由调用栈快照渲染（首行 "Name: message"，随后 "at name (position)"）
// - Error.prototype.toString：name/message 缺省规则
// - Object.prototype.toString tag = "[object Error]"
// - Error 参与运算走 ToPrimitive；call/construct 之外的行为与普通对象一致

#include "core/CallStack.h"
#include "core/IContext.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using neo::core::IContext;
using neo::core::SourcePosition;
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

// 取自有属性的描述符标志（通过 getOwnPropertyNames 判断不可枚举）
bool isEnumerable(IContext* ctx, Value* obj, const char* key) {
    Value* keys = ctx->ownEnumerableStringKeys(obj);
    auto* d = static_cast<neo::core::ArrayData*>(keys->data());
    for (uint32_t i = 0; i < d->length(); ++i) {
        Value* k = d->elementAt(i);
        if (k != nullptr && str(k) == key) return true;
    }
    return false;
}

}  // namespace

// ===== 类型与原型 =====

TEST(ErrorTest, KindAndObjectLayout) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_TRUE(err->type()->hasObjectLayout());
    EXPECT_TRUE(neo::core::kindHasObjectLayout(TypeKind::Error));
}

TEST(ErrorTest, PrototypeIsErrorPrototype) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    Value* proto = f.ctx->getPrototype(err);
    EXPECT_EQ(proto, f.ctx->errorPrototype());
    // %Error.prototype% 的原型是 Object.prototype
    EXPECT_EQ(f.ctx->getPrototype(f.ctx->errorPrototype()), f.ctx->objectPrototype());
    // 双向 constructor
    EXPECT_EQ(f.ctx->getProperty(f.ctx->errorPrototype(), "constructor"),
              f.ctx->global()->lookup("Error"));
}

TEST(ErrorTest, GlobalErrorIsConstructor) {
    Fixture f;
    Value* errorFn = f.ctx->global()->lookup("Error");
    ASSERT_NE(errorFn, nullptr);
    EXPECT_EQ(errorFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(f.ctx->getProperty(errorFn, "prototype"), f.ctx->errorPrototype());
    EXPECT_EQ(str(f.ctx->getProperty(errorFn, "name")), "Error");
}

// ===== message =====

TEST(ErrorTest, CreateWithMessage) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "boom");
}

TEST(ErrorTest, EmptyMessageOmitsProperty) {
    Fixture f;
    Value* err = f.ctx->createError("");
    // 空消息不定义自有 message；读取沿原型链得到 Error.prototype.message = ""
    EXPECT_FALSE(f.ctx->hasProperty(err, "message") &&
                 f.ctx->getProperty(err, "message") !=
                     f.ctx->getProperty(f.ctx->errorPrototype(), "message"));
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "");
}

TEST(ErrorTest, MessageIsWritableNonEnumerableConfigurable) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_FALSE(isEnumerable(f.ctx.get(), err, "message"));
    // writable
    f.ctx->setProperty(err, "message", f.ctx->createString("changed"));
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "changed");
    // configurable
    EXPECT_EQ(f.ctx->deleteProperty(err, "message")->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "");  // 落到原型
}

// ===== stack =====

TEST(ErrorTest, StackHeaderWithoutFrames) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "Error: boom");
}

TEST(ErrorTest, StackHeaderWithoutMessage) {
    Fixture f;
    Value* err = f.ctx->createError("");
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "Error");
}

TEST(ErrorTest, StackRendersCallFramesTopDown) {
    Fixture f;
    f.ctx->pushFrame(SourcePosition("a.js", 1, 1), "main");
    f.ctx->pushFrame(SourcePosition("a.js", 10, 5), "foo");
    f.ctx->pushFrame(SourcePosition("a.js", 20, 9), "bar");

    // 出错点在 bar 内的 a.js:30:1——只有调用方知道，经 position 传入补全栈顶帧
    Value* err = f.ctx->createError("boom", "Error", SourcePosition("a.js", 30, 1));
    std::string stack = str(f.ctx->getProperty(err, "stack"));
    // 首行 + 三帧，最近调用在前
    EXPECT_EQ(stack,
              "Error: boom\n"
              "    at bar (a.js:30:1)\n"
              "    at foo (a.js:20:9)\n"
              "    at main (a.js:10:5)");
    // Context 的栈未被 trace 改动：栈顶帧位置仍未知
    EXPECT_TRUE(f.ctx->callStack().back().position.filename.empty());
}

TEST(ErrorTest, StackRendersInternalFrame) {
    Fixture f;
    f.ctx->pushFrame(neo::core::SourcePosition::internal(), "Array.prototype.map");

    // 出错点在 native 内建里：位置同样是 <internal>
    Value* err = f.ctx->createError("boom", "Error", SourcePosition::internal());
    std::string stack = str(f.ctx->getProperty(err, "stack"));
    EXPECT_EQ(stack, "Error: boom\n    at Array.prototype.map (<internal>)");
}

TEST(ErrorTest, StackTopFrameWithoutPositionOmitsParens) {
    Fixture f;
    f.ctx->pushFrame(SourcePosition("a.js", 1, 1), "main");

    // 未提供出错位置：栈顶帧只输出函数名
    Value* err = f.ctx->createError("boom");
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "Error: boom\n    at main");
}

TEST(ErrorTest, StackUsesGivenName) {
    Fixture f;
    Value* err = f.ctx->createError("bad", "TypeError");
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "TypeError: bad");
}

TEST(ErrorTest, StackIsWritableNonEnumerableConfigurable) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_FALSE(isEnumerable(f.ctx.get(), err, "stack"));
    f.ctx->setProperty(err, "stack", f.ctx->createString("custom"));
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "custom");
    f.ctx->deleteProperty(err, "stack");
    EXPECT_EQ(f.ctx->getProperty(err, "stack")->type()->kind(), TypeKind::Undefined);
}

// ===== 构造函数 =====

TEST(ErrorTest, ConstructWithMessage) {
    Fixture f;
    Value* errorFn = f.ctx->global()->lookup("Error");
    Value* args[] = {f.ctx->createString("from new")};
    Value* err = f.ctx->construct(errorFn, 1, args);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "from new");
    EXPECT_EQ(f.ctx->getPrototype(err), f.ctx->errorPrototype());
}

TEST(ErrorTest, ConstructWithoutArgsOmitsMessage) {
    Fixture f;
    Value* errorFn = f.ctx->global()->lookup("Error");
    Value* err = f.ctx->construct(errorFn, 0, nullptr);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "");  // 原型上的 ""
}

TEST(ErrorTest, CallSameAsConstruct) {
    Fixture f;
    Value* errorFn = f.ctx->global()->lookup("Error");
    Value* args[] = {f.ctx->createString("from call")};
    Value* err = f.ctx->call(errorFn, nullptr, 1, args);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "from call");
}

TEST(ErrorTest, ConstructToStringsMessage) {
    Fixture f;
    Value* errorFn = f.ctx->global()->lookup("Error");
    Value* args[] = {f.ctx->createNumber(42)};
    Value* err = f.ctx->construct(errorFn, 1, args);
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "42");
}

// ===== toString =====

TEST(ErrorTest, ToStringNameAndMessage) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    Value* s = f.ctx->call(f.ctx->getProperty(err, "toString"), err, 0, nullptr);
    EXPECT_EQ(str(s), "Error: boom");
}

TEST(ErrorTest, ToStringEmptyMessageReturnsName) {
    Fixture f;
    Value* err = f.ctx->createError("");
    Value* s = f.ctx->call(f.ctx->getProperty(err, "toString"), err, 0, nullptr);
    EXPECT_EQ(str(s), "Error");
}

TEST(ErrorTest, ToStringEmptyNameReturnsMessage) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    f.ctx->setProperty(err, "name", f.ctx->createString(""));
    Value* s = f.ctx->call(f.ctx->getProperty(err, "toString"), err, 0, nullptr);
    EXPECT_EQ(str(s), "boom");
}

// ===== 对象模型集成 =====

TEST(ErrorTest, ObjectProtoToStringTag) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    Value* toString = f.ctx->getProperty(f.ctx->objectPrototype(), "toString");
    Value* s = f.ctx->call(toString, err, 0, nullptr);
    EXPECT_EQ(str(s), "[object Error]");
}

TEST(ErrorTest, ArithmeticUsesToPrimitive) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    // ToPrimitive 默认 hint → valueOf（对象自身）→ toString → "Error: boom"
    Value* sum = f.ctx->add(err, f.ctx->createString("!"));
    ASSERT_EQ(sum->type()->kind(), TypeKind::String);
    EXPECT_EQ(str(sum), "Error: boom!");
}

TEST(ErrorTest, NotCallableNorConstructor) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    EXPECT_EQ(f.ctx->call(err, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(f.ctx->construct(err, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ErrorTest, CustomPropertiesWork) {
    Fixture f;
    Value* err = f.ctx->createError("boom");
    f.ctx->setProperty(err, "code", f.ctx->createNumber(404));
    Value* code = f.ctx->getProperty(err, "code");
    ASSERT_EQ(code->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(code->data())->value, 404.0);
    EXPECT_TRUE(isEnumerable(f.ctx.get(), err, "code"));
}

TEST(ErrorTest, ErrorPrototypeHasNoOwnStack) {
    Fixture f;
    // 原型对象不应有自有 stack（ES2024 §20.5.3）
    EXPECT_EQ(f.ctx->getProperty(f.ctx->errorPrototype(), "stack")->type()->kind(),
              TypeKind::Undefined);
}

TEST(ErrorTest, ErrorPrototypeIsPlainObject) {
    Fixture f;
    // %Error.prototype% 是 ordinary object，没有 [[ErrorData]]（ES2024 §20.5.3）
    EXPECT_EQ(f.ctx->errorPrototype()->type()->kind(), TypeKind::Object);
}

// ===== NativeError 子类（ES2024 §20.5.6）=====

namespace {

const char* kNativeErrorNames[] = {"TypeError", "RangeError", "ReferenceError",
                                   "SyntaxError", "URIError"};

}  // namespace

TEST(ErrorTest, NativeErrorConstructorsRegistered) {
    Fixture f;
    for (const char* name : kNativeErrorNames) {
        Value* fn = f.ctx->global()->lookup(name);
        ASSERT_NE(fn, nullptr) << name;
        EXPECT_EQ(fn->type()->kind(), TypeKind::Function) << name;
        EXPECT_EQ(str(f.ctx->getProperty(fn, "name")), name);
        // 子类原型的原型是 %Error.prototype%
        Value* proto = f.ctx->getProperty(fn, "prototype");
        ASSERT_NE(proto, nullptr) << name;
        EXPECT_EQ(f.ctx->getPrototype(proto), f.ctx->errorPrototype()) << name;
        // 子类原型是普通对象（无 [[ErrorData]]，§20.5.6.3）
        EXPECT_EQ(proto->type()->kind(), TypeKind::Object) << name;
        // 自有 name + 双向 constructor
        EXPECT_EQ(str(f.ctx->getProperty(proto, "name")), name) << name;
        EXPECT_EQ(f.ctx->getProperty(proto, "constructor"), fn) << name;
    }
}

TEST(ErrorTest, EvalErrorNotImplemented) {
    Fixture f;
    // EvalError 已不在 ES2024 规范中
    EXPECT_EQ(f.ctx->global()->lookup("EvalError"), nullptr);
    EXPECT_EQ(f.ctx->global()->lookup("AggregateError"), nullptr);
}

TEST(ErrorTest, NativeErrorConstructWithMessage) {
    Fixture f;
    for (const char* name : kNativeErrorNames) {
        Value* fn = f.ctx->global()->lookup(name);
        Value* args[] = {f.ctx->createString("bad")};
        Value* err = f.ctx->construct(fn, 1, args);
        ASSERT_EQ(err->type()->kind(), TypeKind::Error) << name;
        EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "bad") << name;
        // 实例原型是该子类原型，不是 %Error.prototype%
        EXPECT_EQ(f.ctx->getPrototype(err), f.ctx->getProperty(fn, "prototype")) << name;
        // name 沿原型链读到自己子类的 name
        EXPECT_EQ(str(f.ctx->getProperty(err, "name")), name) << name;
    }
}

TEST(ErrorTest, NativeErrorCallSameAsConstruct) {
    Fixture f;
    Value* fn = f.ctx->global()->lookup("TypeError");
    Value* args[] = {f.ctx->createString("bad")};
    Value* err = f.ctx->call(fn, nullptr, 1, args);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    EXPECT_EQ(str(f.ctx->getProperty(err, "name")), "TypeError");
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "bad");
}

TEST(ErrorTest, NativeErrorWithoutMessageOmitsProperty) {
    Fixture f;
    Value* fn = f.ctx->global()->lookup("RangeError");
    Value* err = f.ctx->construct(fn, 0, nullptr);
    ASSERT_EQ(err->type()->kind(), TypeKind::Error);
    // 自有 message 不存在，沿原型链读到 Error.prototype.message = ""
    EXPECT_EQ(str(f.ctx->getProperty(err, "message")), "");
    EXPECT_FALSE(f.ctx->hasProperty(err, "message") &&
                 f.ctx->getProperty(err, "message") !=
                     f.ctx->getProperty(f.ctx->errorPrototype(), "message"));
}

TEST(ErrorTest, NativeErrorStackUsesSubclassName) {
    Fixture f;
    Value* fn = f.ctx->global()->lookup("SyntaxError");
    Value* args[] = {f.ctx->createString("unexpected token")};
    Value* err = f.ctx->construct(fn, 1, args);
    EXPECT_EQ(str(f.ctx->getProperty(err, "stack")), "SyntaxError: unexpected token");
}

TEST(ErrorTest, NativeErrorToString) {
    Fixture f;
    Value* fn = f.ctx->global()->lookup("URIError");
    Value* args[] = {f.ctx->createString("bad uri")};
    Value* err = f.ctx->construct(fn, 1, args);
    // toString 继承自 %Error.prototype%
    Value* s = f.ctx->call(f.ctx->getProperty(err, "toString"), err, 0, nullptr);
    EXPECT_EQ(str(s), "URIError: bad uri");
}

TEST(ErrorTest, NativeErrorObjectProtoToStringTag) {
    Fixture f;
    Value* fn = f.ctx->global()->lookup("TypeError");
    Value* err = f.ctx->construct(fn, 0, nullptr);
    Value* toString = f.ctx->getProperty(f.ctx->objectPrototype(), "toString");
    Value* s = f.ctx->call(toString, err, 0, nullptr);
    EXPECT_EQ(str(s), "[object Error]");
}

TEST(ErrorTest, NativeErrorInstanceOfViaPrototypeChain) {
    Fixture f;
    Value* typeFn = f.ctx->global()->lookup("TypeError");
    Value* err = f.ctx->construct(typeFn, 0, nullptr);
    // 原型链：err → TypeError.prototype → Error.prototype → Object.prototype
    Value* chain[3] = {f.ctx->getProperty(typeFn, "prototype"), f.ctx->errorPrototype(),
                       f.ctx->objectPrototype()};
    Value* cur = f.ctx->getPrototype(err);
    int i = 0;
    for (; cur != nullptr && i < 3; cur = f.ctx->getPrototype(cur), ++i) {
        EXPECT_EQ(cur, chain[i]) << "chain depth " << i;
    }
    EXPECT_EQ(i, 3);
}
