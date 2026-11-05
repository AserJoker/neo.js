// ObjectBuiltinsTest.cpp - Object / Function 构造函数与内建原型单元测试
//
// 验证（ADR-035 / ES2024 §20.1 / §20.2）：
// - Object.prototype / Function.prototype 建立，原型链闭合
// - 新对象自动挂 Object.prototype；新函数自动挂 Function.prototype
// - Object(value) 语义：无参/undefined/null → 空对象；对象/函数 → 原样返回；
//   原始值 → Exception（包装对象未实现）
// - Function(...) / new Function → Exception（not implemented）
// - Object.prototype: toString / valueOf / hasOwnProperty
// - Function.prototype: toString / call / apply / bind
// - 构造函数与 prototype 双向引用
// - GC：内建原型与构造函数存活
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::ObjectData;
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

std::string strOf(Value* v) {
    return static_cast<StringData*>(v->data())->value;
}

double numOf(Value* v) {
    return static_cast<NumberData*>(v->data())->value;
}

Value* globalOf(IContext* ctx, const char* name) {
    return ctx->global()->lookup(name);
}

// 供 Function.prototype.call/apply 测试用
Value* returnsThis(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return self != nullptr ? self : ctx->createUndefined();
}

Value* sumArgs(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    double sum = 0;
    for (size_t i = 0; i < argc; ++i) sum += numOf(argv[i]);
    return ctx->createNumber(sum);
}

}  // namespace

// ===== 原型链建立 =====

TEST(ObjectBuiltinsTest, ObjectPrototypeExists) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->objectPrototype();
    ASSERT_NE(proto, nullptr);
    EXPECT_EQ(proto->type()->kind(), TypeKind::Object);
    // Object.prototype 是原型链终点
    EXPECT_EQ(ctx->getPrototype(proto)->type()->kind(), TypeKind::Null);
}

TEST(ObjectBuiltinsTest, FunctionPrototypeInheritsObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fproto = ctx->functionPrototype();
    ASSERT_NE(fproto, nullptr);
    EXPECT_EQ(fproto->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getPrototype(fproto), ctx->objectPrototype());
}

TEST(ObjectBuiltinsTest, NewObjectInheritsObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->getPrototype(obj), ctx->objectPrototype());
}

TEST(ObjectBuiltinsTest, NewFunctionInheritsFunctionPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    EXPECT_EQ(ctx->getPrototype(fn), ctx->functionPrototype());
}

TEST(ObjectBuiltinsTest, ConstructorPrototypeBackReferences) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* functionFn = globalOf(ctx.get(), "Function");
    ASSERT_NE(objectFn, nullptr);
    ASSERT_NE(functionFn, nullptr);

    // Object.prototype === objectFn.prototype，且 objectProto.constructor === Object
    EXPECT_EQ(ctx->getProperty(objectFn, "prototype"), ctx->objectPrototype());
    EXPECT_EQ(ctx->getProperty(ctx->objectPrototype(), "constructor"), objectFn);
    // Function 同理
    EXPECT_EQ(ctx->getProperty(functionFn, "prototype"), ctx->functionPrototype());
    EXPECT_EQ(ctx->getProperty(ctx->functionPrototype(), "constructor"), functionFn);
}

TEST(ObjectBuiltinsTest, ConstructorsInheritFunctionPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(ctx->getPrototype(globalOf(ctx.get(), "Object")), ctx->functionPrototype());
    EXPECT_EQ(ctx->getPrototype(globalOf(ctx.get(), "Function")), ctx->functionPrototype());
}

// ===== Object(value) 语义 =====

TEST(ObjectBuiltinsTest, ObjectNoArgsCreatesEmptyObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->call(globalOf(ctx.get(), "Object"), nullptr, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->properties.empty());
}

TEST(ObjectBuiltinsTest, ObjectUndefinedOrNullCreatesEmptyObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* args1[1] = {ctx->createUndefined()};
    EXPECT_EQ(ctx->call(objectFn, nullptr, 1, args1)->type()->kind(), TypeKind::Object);
    Value* args2[1] = {ctx->createNull()};
    EXPECT_EQ(ctx->call(objectFn, nullptr, 1, args2)->type()->kind(), TypeKind::Object);
}

TEST(ObjectBuiltinsTest, ObjectReturnsObjectAsIs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(globalOf(ctx.get(), "Object"), nullptr, 1, args), obj);
}

TEST(ObjectBuiltinsTest, ObjectReturnsFunctionAsIs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* args[1] = {fn};
    EXPECT_EQ(ctx->call(globalOf(ctx.get(), "Object"), nullptr, 1, args), fn);
}

TEST(ObjectBuiltinsTest, ObjectPrimitiveWrappingNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* args[1] = {ctx->createNumber(1)};
    EXPECT_EQ(ctx->call(objectFn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
}

// ===== Function 构造函数占位 =====

TEST(ObjectBuiltinsTest, FunctionCallIsNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* functionFn = globalOf(ctx.get(), "Function");
    ASSERT_EQ(functionFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->call(functionFn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, NewFunctionIsNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(ctx->construct(globalOf(ctx.get(), "Function"), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

// ===== Object.prototype 方法 =====

TEST(ObjectBuiltinsTest, ObjectProtoToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    ASSERT_EQ(toStringFn->type()->kind(), TypeKind::Function);

    Value* obj = ctx->createObject();
    EXPECT_EQ(strOf(ctx->call(toStringFn, obj, 0, nullptr)), "[object Object]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createNumber(1), 0, nullptr)),
              "[object Number]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createString("s"), 0, nullptr)),
              "[object String]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createBoolean(true), 0, nullptr)),
              "[object Boolean]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createSymbol("k"), 0, nullptr)),
              "[object Symbol]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createNativeFunction(returnsThis, 0), 0,
                              nullptr)),
              "[object Function]");
}

TEST(ObjectBuiltinsTest, ObjectProtoValueOfReturnsSelf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* valueOfFn = ctx->getProperty(ctx->objectPrototype(), "valueOf");
    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->call(valueOfFn, obj, 0, nullptr), obj);
}

TEST(ObjectBuiltinsTest, ObjectProtoHasOwnProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(1));

    Value* args1[1] = {ctx->createString("x")};
    EXPECT_EQ(ctx->call(hasOwn, obj, 1, args1)->type()->kind(), TypeKind::Boolean);
    Value* args2[1] = {ctx->createString("y")};
    EXPECT_EQ(ctx->call(hasOwn, obj, 1, args2)->type()->kind(), TypeKind::Boolean);

    // 原型链上的属性不算自有
    Value* args3[1] = {ctx->createString("toString")};
    EXPECT_EQ(ctx->call(hasOwn, obj, 1, args3)->type()->kind(), TypeKind::Boolean);
    auto* b = static_cast<neo::core::BooleanData*>(
        ctx->call(hasOwn, obj, 1, args3)->data());
    EXPECT_FALSE(b->value);
}

TEST(ObjectBuiltinsTest, ObjectProtoHasOwnPropertySymbolKey) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("k");
    ctx->setProperty(obj, sym, ctx->createNumber(1));

    Value* args[1] = {sym};
    auto* b = static_cast<neo::core::BooleanData*>(ctx->call(hasOwn, obj, 1, args)->data());
    EXPECT_TRUE(b->value);
}

TEST(ObjectBuiltinsTest, ObjectProtoHasOwnPropertyRejectsNonObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* args[1] = {ctx->createString("x")};
    EXPECT_EQ(ctx->call(hasOwn, ctx->createNumber(1), 1, args)->type()->kind(),
              TypeKind::Exception);
}

// ===== Function.prototype 方法 =====

TEST(ObjectBuiltinsTest, FunctionProtoToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->functionPrototype(), "toString");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    EXPECT_EQ(strOf(ctx->call(toStringFn, fn, 0, nullptr)),
              "function () { [native code] }");
}

TEST(ObjectBuiltinsTest, FunctionProtoCall) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* callFn = ctx->getProperty(ctx->functionPrototype(), "call");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* thisArg = ctx->createObject();
    Value* args[1] = {thisArg};
    EXPECT_EQ(ctx->call(callFn, fn, 1, args), thisArg);
}

TEST(ObjectBuiltinsTest, FunctionProtoCallForwardsArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* callFn = ctx->getProperty(ctx->functionPrototype(), "call");
    Value* fn = ctx->createNativeFunction(sumArgs, 0);
    Value* args[3] = {ctx->createNull(), ctx->createNumber(1), ctx->createNumber(2)};
    Value* res = ctx->call(callFn, fn, 3, args);
    EXPECT_DOUBLE_EQ(numOf(res), 3.0);
}

TEST(ObjectBuiltinsTest, FunctionProtoCallRejectsNonFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* callFn = ctx->getProperty(ctx->functionPrototype(), "call");
    EXPECT_EQ(ctx->call(callFn, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithNullArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* thisArg = ctx->createObject();
    Value* args[2] = {thisArg, ctx->createNull()};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args), thisArg);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithArrayNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* args[2] = {ctx->createNull(), ctx->createObject()};  // 非 null → 需 Array
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyRejectsNonFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    EXPECT_EQ(ctx->call(applyFn, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoBindNotImplemented) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* args[1] = {ctx->createObject()};
    EXPECT_EQ(ctx->call(bindFn, fn, 1, args)->type()->kind(), TypeKind::Exception);
}

// ===== 原型链上的方法可被实例访问 =====

TEST(ObjectBuiltinsTest, InstanceInheritsPrototypeMethods) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // 通过原型链读到 toString / valueOf / hasOwnProperty
    EXPECT_EQ(ctx->getProperty(obj, "toString")->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getProperty(obj, "valueOf")->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getProperty(obj, "hasOwnProperty")->type()->kind(), TypeKind::Function);
    EXPECT_TRUE(ctx->hasProperty(obj, "toString"));
    // 不是自有属性
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_EQ(d->properties.count("toString"), 0u);
}

TEST(ObjectBuiltinsTest, FunctionInheritsFunctionPrototypeMethods) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    EXPECT_EQ(ctx->getProperty(fn, "call")->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getProperty(fn, "apply")->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getProperty(fn, "bind")->type()->kind(), TypeKind::Function);
}

// ===== 描述符 =====

TEST(ObjectBuiltinsTest, PrototypeMethodsAreNotEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* op = static_cast<ObjectData*>(ctx->objectPrototype()->data());
    for (const char* name : {"toString", "valueOf", "hasOwnProperty"}) {
        const auto& p = op->properties.at(name);
        EXPECT_TRUE(p.writable);
        EXPECT_FALSE(p.enumerable);
        EXPECT_TRUE(p.configurable);
    }
    auto* fp = static_cast<ObjectData*>(ctx->functionPrototype()->data());
    for (const char* name : {"toString", "call", "apply", "bind"}) {
        const auto& p = fp->properties.at(name);
        EXPECT_TRUE(p.writable);
        EXPECT_FALSE(p.enumerable);
        EXPECT_TRUE(p.configurable);
    }
}

TEST(ObjectBuiltinsTest, ConstructorPrototypePropertyIsReadOnly) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    auto* od = static_cast<ObjectData*>(globalOf(ctx.get(), "Object")->data());
    const auto& p = od->properties.at("prototype");
    EXPECT_FALSE(p.writable);
    EXPECT_FALSE(p.enumerable);
    EXPECT_FALSE(p.configurable);
}

// ===== GC =====

TEST(ObjectBuiltinsTest, BuiltinsSurviveGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectProto = ctx->objectPrototype();
    Value* functionProto = ctx->functionPrototype();
    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* functionFn = globalOf(ctx.get(), "Function");

    ctx->gc()->collect();
    EXPECT_EQ(ctx->objectPrototype(), objectProto);
    EXPECT_EQ(ctx->functionPrototype(), functionProto);
    EXPECT_EQ(globalOf(ctx.get(), "Object"), objectFn);
    EXPECT_EQ(globalOf(ctx.get(), "Function"), functionFn);
    // 原型链仍完整
    EXPECT_EQ(ctx->getPrototype(ctx->createObject()), objectProto);
}

// ===== 边界分支补充 =====

TEST(ObjectBuiltinsTest, ObjectProtoToStringUndefinedAndNull) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createUndefined(), 0, nullptr)),
              "[object Undefined]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createNull(), 0, nullptr)),
              "[object Null]");
    // this 为 nullptr（native 直接调用）也返回 Undefined
    EXPECT_EQ(strOf(ctx->call(toStringFn, nullptr, 0, nullptr)), "[object Undefined]");
}

TEST(ObjectBuiltinsTest, ObjectProtoValueOfWithNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* valueOfFn = ctx->getProperty(ctx->objectPrototype(), "valueOf");
    EXPECT_EQ(ctx->call(valueOfFn, nullptr, 0, nullptr)->type()->kind(),
              TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, HasOwnPropertyWithoutArgsReturnsFalse) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* obj = ctx->createObject();
    auto* b = static_cast<neo::core::BooleanData*>(
        ctx->call(hasOwn, obj, 0, nullptr)->data());
    EXPECT_FALSE(b->value);
}

TEST(ObjectBuiltinsTest, HasOwnPropertyWithNullThisThrows) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* args[1] = {ctx->createString("x")};
    EXPECT_EQ(ctx->call(hasOwn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, HasOwnPropertyOnFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    // 函数自有 name/prototype 属性
    Value* args1[1] = {ctx->createString("name")};
    auto* b1 = static_cast<neo::core::BooleanData*>(ctx->call(hasOwn, fn, 1, args1)->data());
    EXPECT_TRUE(b1->value);
    // 原型链上的 call 不是自有
    Value* args2[1] = {ctx->createString("call")};
    auto* b2 = static_cast<neo::core::BooleanData*>(ctx->call(hasOwn, fn, 1, args2)->data());
    EXPECT_FALSE(b2->value);
}

TEST(ObjectBuiltinsTest, FunctionProtoCallWithoutArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* callFn = ctx->getProperty(ctx->functionPrototype(), "call");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    // 无 thisArg → this 为 undefined
    EXPECT_EQ(ctx->call(callFn, fn, 0, nullptr)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, FunctionProtoCallWithNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* callFn = ctx->getProperty(ctx->functionPrototype(), "call");
    EXPECT_EQ(ctx->call(callFn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithoutArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    EXPECT_EQ(ctx->call(applyFn, fn, 0, nullptr)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithUndefinedArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* thisArg = ctx->createObject();
    Value* args[2] = {thisArg, ctx->createUndefined()};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args), thisArg);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    EXPECT_EQ(ctx->call(applyFn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}
