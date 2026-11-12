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
#include "TestHelpers.h"

#include <cmath>

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

// 返回实参个数（apply/bind 展开验证用）
Value* argcFn(IContext* ctx, Value* /*self*/, size_t argc, Value* /*argv*/[]) {
    return ctx->createNumber(static_cast<double>(argc));
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

TEST(ObjectBuiltinsTest, ObjectPrimitiveWrappingReturnsWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* args[1] = {ctx->createNumber(1)};
    Value* wrapper = ctx->call(objectFn, nullptr, 1, args);
    EXPECT_EQ(wrapper->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(ctx->getPrototype(wrapper), ctx->numberPrototype());
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

TEST(ObjectBuiltinsTest, FunctionProtoApplyWithArrayLike) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    // 数组实参展开
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* args[2] = {ctx->createNull(), arr};
    Value* r = ctx->call(applyFn, fn, 2, args);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(r->data())->value, 2.0);

    // 类数组对象（有 length 与索引）同样展开
    Value* arrayLike = ctx->createObject();
    ctx->setProperty(arrayLike, "length", ctx->createNumber(3));
    ctx->setProperty(arrayLike, "0", ctx->createNumber(1));
    ctx->setProperty(arrayLike, "1", ctx->createNumber(2));
    ctx->setProperty(arrayLike, "2", ctx->createNumber(3));
    Value* args2[2] = {ctx->createNull(), arrayLike};
    Value* r2 = ctx->call(applyFn, fn, 2, args2);
    ASSERT_EQ(r2->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(r2->data())->value, 3.0);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyRejectsPrimitiveArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* args[2] = {ctx->createNull(), ctx->createNumber(1)};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoApplyErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    // length getter 抛异常 → 透传
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    Value* arrayLike = ctx->createObject();
    ctx->defineAccessor(arrayLike, "length", boom, nullptr);
    Value* args[2] = {ctx->createNull(), arrayLike};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args)->type()->kind(), TypeKind::Exception);

    // 索引 getter 抛异常 → 透传
    Value* arrayLike2 = ctx->createObject();
    ctx->setProperty(arrayLike2, "length", ctx->createNumber(1));
    ctx->defineAccessor(arrayLike2, "0", boom, nullptr);
    Value* args2[2] = {ctx->createNull(), arrayLike2};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args2)->type()->kind(), TypeKind::Exception);

    // length 非数字/负数 → 按 0 处理
    Value* arrayLike3 = ctx->createObject();
    ctx->setProperty(arrayLike3, "length", ctx->createString("abc"));
    Value* args3[2] = {ctx->createNull(), arrayLike3};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args3)->type()->kind(), TypeKind::Null);
    Value* arrayLike4 = ctx->createObject();
    ctx->setProperty(arrayLike4, "length", ctx->createNumber(-1));
    Value* args4[2] = {ctx->createNull(), arrayLike4};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args4)->type()->kind(), TypeKind::Null);
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

TEST(ObjectBuiltinsTest, FunctionProtoBindReturnsBoundFunction) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* boundThis = ctx->createObject();
    Value* args[1] = {boundThis};
    Value* bound = ctx->call(bindFn, fn, 1, args);
    ASSERT_EQ(bound->type()->kind(), TypeKind::Function);
    // 调用绑定函数：this 固定为 boundThis
    EXPECT_EQ(ctx->call(bound, nullptr, 0, nullptr), boundThis);
    // name = "bound " + target.name
    Value* name = ctx->getProperty(bound, "name");
    ASSERT_EQ(name->type()->kind(), TypeKind::String);
    EXPECT_EQ(static_cast<neo::core::StringData*>(name->data())->value,
              "bound <anonymous>");
}

TEST(ObjectBuiltinsTest, FunctionProtoBindPrependsArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    Value* args[2] = {ctx->createNull(), ctx->createNumber(1)};
    Value* bound = ctx->call(bindFn, fn, 2, args);
    ASSERT_EQ(bound->type()->kind(), TypeKind::Function);

    // 前置 1 个实参 + 调用时 2 个 → argc = 3
    Value* callArgs[2] = {ctx->createNumber(2), ctx->createNumber(3)};
    Value* r = ctx->call(bound, nullptr, 2, callArgs);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(r->data())->value, 3.0);

    // length = max(0, target.length - boundArgs) = max(0, 0 - 1) = 0
    Value* len = ctx->getProperty(bound, "length");
    ASSERT_EQ(len->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(static_cast<neo::core::NumberData*>(len->data())->value, 0.0);
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
    EXPECT_TRUE(neo::test::hasProperty(ctx, obj, "toString"));
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

// ===== Object 静态方法 =====

namespace {

// 取数组元素（越界/空洞按 undefined 处理）
Value* elemOf(IContext* ctx, Value* arr, size_t i) {
    return ctx->getProperty(arr, ctx->createString(std::to_string(i)));
}

bool boolOf(Value* v) {
    return static_cast<neo::core::BooleanData*>(v->data())->value;
}

// 调用 Object.<name>(...)
Value* callObjectStatic(IContext* ctx, const char* name, size_t argc, Value* argv[]) {
    Value* objFn = globalOf(ctx, "Object");
    Value* fn = ctx->getProperty(objFn, name);
    return ctx->call(fn, objFn, argc, argv);
}

}  // namespace

TEST(ObjectBuiltinsTest, ObjectKeysReturnsEnumerableStringKeys) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "b", ctx->createNumber(1));
    ctx->setProperty(obj, "a", ctx->createNumber(2));
    Value* args[1] = {obj};
    Value* keys = callObjectStatic(ctx.get(), "keys", 1, args);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 0)), "b");  // 插入序
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 1)), "a");
    EXPECT_EQ(elemOf(ctx.get(), keys, 2)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, ObjectKeysOrdersIndicesAscendingFirst) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "x", ctx->createNumber(0));
    ctx->setProperty(obj, "2", ctx->createNumber(1));
    ctx->setProperty(obj, "0", ctx->createNumber(2));
    Value* args[1] = {obj};
    Value* keys = callObjectStatic(ctx.get(), "keys", 1, args);
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 0)), "0");
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 1)), "2");
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 2)), "x");
}

TEST(ObjectBuiltinsTest, ObjectKeysSkipsNonEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "hidden", ctx->createNumber(1), true, false, true);
    ctx->setProperty(obj, "shown", ctx->createNumber(2));
    Value* args[1] = {obj};
    Value* keys = callObjectStatic(ctx.get(), "keys", 1, args);
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 0)), "shown");
    EXPECT_EQ(elemOf(ctx.get(), keys, 1)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, ObjectKeysRejectsNullAndUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* args[1] = {ctx->createNull()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "keys", 1, args)->type()->kind(), TypeKind::Exception);
    Value* args2[1] = {ctx->createUndefined()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "keys", 1, args2)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectKeysOnStringWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 原始值经 ToObject 包装：索引 0/1 可枚举
    Value* args[1] = {ctx->createString("ab")};
    Value* keys = callObjectStatic(ctx.get(), "keys", 1, args);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 0)), "0");
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 1)), "1");
    EXPECT_EQ(elemOf(ctx.get(), keys, 2)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, ObjectValuesAndEntries) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "a", ctx->createNumber(1));
    ctx->setProperty(obj, "b", ctx->createNumber(2));

    Value* args[1] = {obj};
    Value* values = callObjectStatic(ctx.get(), "values", 1, args);
    ASSERT_EQ(values->type()->kind(), TypeKind::Array);
    EXPECT_DOUBLE_EQ(numOf(elemOf(ctx.get(), values, 0)), 1.0);
    EXPECT_DOUBLE_EQ(numOf(elemOf(ctx.get(), values, 1)), 2.0);

    Value* entries = callObjectStatic(ctx.get(), "entries", 1, args);
    ASSERT_EQ(entries->type()->kind(), TypeKind::Array);
    Value* first = elemOf(ctx.get(), entries, 0);
    ASSERT_EQ(first->type()->kind(), TypeKind::Array);
    EXPECT_EQ(strOf(elemOf(ctx.get(), first, 0)), "a");
    EXPECT_DOUBLE_EQ(numOf(elemOf(ctx.get(), first, 1)), 1.0);
    Value* second = elemOf(ctx.get(), entries, 1);
    EXPECT_EQ(strOf(elemOf(ctx.get(), second, 0)), "b");
    EXPECT_DOUBLE_EQ(numOf(elemOf(ctx.get(), second, 1)), 2.0);
}

TEST(ObjectBuiltinsTest, ObjectAssignCopiesOwnEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();
    Value* src = ctx->createObject();
    ctx->setProperty(src, "a", ctx->createNumber(1));
    ctx->defineProperty(src, "hidden", ctx->createNumber(2), true, false, true);
    Value* args[2] = {target, src};
    Value* r = callObjectStatic(ctx.get(), "assign", 2, args);
    EXPECT_EQ(r, target);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(target, "a")), 1.0);
    EXPECT_EQ(neo::test::hasProperty(ctx, target, "hidden"), false);

    // undefined/null 源被跳过
    Value* args2[3] = {target, ctx->createNull(), ctx->createUndefined()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 3, args2), target);

    // 单参（无源）→ 原样返回 target（ES2024 §20.1.2.1）
    Value* only[1] = {target};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 1, only), target);
}

TEST(ObjectBuiltinsTest, ObjectAssignRejectsNullTarget) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* args[1] = {ctx->createNull()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 1, args)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectCreateSetsPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    ctx->setProperty(proto, "inherited", ctx->createNumber(7));
    Value* args[1] = {proto};
    Value* obj = callObjectStatic(ctx.get(), "create", 1, args);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(obj), proto);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "inherited")), 7.0);

    // null 原型
    Value* args2[1] = {ctx->createNull()};
    Value* bare = callObjectStatic(ctx.get(), "create", 1, args2);
    EXPECT_EQ(ctx->getPrototype(bare)->type()->kind(), TypeKind::Null);

    // 非对象原型 → Exception
    Value* args3[1] = {ctx->createNumber(1)};
    EXPECT_EQ(callObjectStatic(ctx.get(), "create", 1, args3)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectGetAndSetPrototypeOf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* args[1] = {obj};
    EXPECT_EQ(callObjectStatic(ctx.get(), "getPrototypeOf", 1, args), ctx->objectPrototype());

    Value* proto = ctx->createObject();
    Value* args2[2] = {obj, proto};
    EXPECT_EQ(callObjectStatic(ctx.get(), "setPrototypeOf", 2, args2), obj);
    EXPECT_EQ(ctx->getPrototype(obj), proto);

    // null 原型合法
    Value* argsNull[2] = {obj, ctx->createNull()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "setPrototypeOf", 2, argsNull), obj);
    EXPECT_EQ(ctx->getPrototype(obj)->type()->kind(), TypeKind::Null);

    // 非法原型 → Exception
    Value* args3[2] = {obj, ctx->createNumber(1)};
    EXPECT_EQ(callObjectStatic(ctx.get(), "setPrototypeOf", 2, args3)->type()->kind(),
              TypeKind::Exception);
    // null/undefined 目标 → Exception
    Value* args4[1] = {ctx->createNull()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "getPrototypeOf", 1, args4)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectDefinePropertyWithDescriptor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* desc = ctx->createObject();
    ctx->setProperty(desc, "value", ctx->createNumber(5));
    ctx->setProperty(desc, "writable", ctx->createBoolean(true));
    ctx->setProperty(desc, "enumerable", ctx->createBoolean(false));
    Value* args[3] = {obj, ctx->createString("x"), desc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args), obj);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 5.0);
    Value* isEnum = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    Value* enumArg[1] = {ctx->createString("x")};
    EXPECT_FALSE(boolOf(ctx->call(isEnum, obj, 1, enumArg)));

    // 访问器描述符：getter 以 obj 为 this 调用
    Value* getter = ctx->createNativeFunction(returnsThis, 0);
    Value* accDesc = ctx->createObject();
    ctx->setProperty(accDesc, "get", getter);
    Value* args2[3] = {obj, ctx->createString("y"), accDesc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args2), obj);
    EXPECT_EQ(ctx->getProperty(obj, "y"), obj);
}

TEST(ObjectBuiltinsTest, ObjectDefinePropertyMergesPartialDescriptor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "x", ctx->createNumber(1), true, true, true);

    // 只给 value：writable/enumerable/configurable 沿用当前值。
    Value* desc = ctx->createObject();
    ctx->setProperty(desc, "value", ctx->createNumber(2));
    Value* args[3] = {obj, ctx->createString("x"), desc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args), obj);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 2.0);

    Value* isEnum = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    Value* enumArg[1] = {ctx->createString("x")};
    EXPECT_TRUE(boolOf(ctx->call(isEnum, obj, 1, enumArg)));

    // writable 仍为 true：普通赋值可覆盖。
    ctx->setProperty(obj, "x", ctx->createNumber(3));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 3.0);

    // 显式 {value: undefined} 与字段缺失不同：应把值改为 undefined。
    Value* undefDesc = ctx->createObject();
    ctx->setProperty(undefDesc, "value", ctx->createUndefined());
    Value* args2[3] = {obj, ctx->createString("x"), undefDesc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args2), obj);
    EXPECT_EQ(ctx->getProperty(obj, "x")->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, ObjectDefinePropertyRejectsBadArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    // 参数不足
    Value* args[2] = {obj, ctx->createString("x")};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 2, args)->type()->kind(),
              TypeKind::Exception);
    // 描述符非对象
    Value* args2[3] = {obj, ctx->createString("x"), ctx->createNumber(1)};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args2)->type()->kind(),
              TypeKind::Exception);
    // 目标 null
    Value* args3[3] = {ctx->createNull(), ctx->createString("x"), ctx->createObject()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args3)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectGetOwnPropertyNamesIncludesNonEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "hidden", ctx->createNumber(1), true, false, true);
    ctx->setProperty(obj, "shown", ctx->createNumber(2));
    Value* args[1] = {obj};
    Value* names = callObjectStatic(ctx.get(), "getOwnPropertyNames", 1, args);
    ASSERT_EQ(names->type()->kind(), TypeKind::Array);
    EXPECT_EQ(strOf(elemOf(ctx.get(), names, 0)), "hidden");
    EXPECT_EQ(strOf(elemOf(ctx.get(), names, 1)), "shown");
}

// ===== 枚举接口（ownPropertyKeys）=====

TEST(ObjectBuiltinsTest, OwnPropertyKeysFullOrder) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "z", ctx->createNumber(0));
    ctx->setProperty(obj, "3", ctx->createNumber(1));
    ctx->setProperty(obj, "1", ctx->createNumber(2));
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(obj, sym, ctx->createNumber(3), true, true, true);

    Value* keys = ctx->ownPropertyKeys(obj, false, true);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 0)), "1");
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 1)), "3");
    EXPECT_EQ(strOf(elemOf(ctx.get(), keys, 2)), "z");
    EXPECT_EQ(elemOf(ctx.get(), keys, 3), sym);  // symbol 保持身份
    EXPECT_EQ(elemOf(ctx.get(), keys, 4)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, OwnPropertyKeysFiltersAndSymbols) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "shown", ctx->createNumber(1));
    ctx->defineProperty(obj, "hidden", ctx->createNumber(2), true, false, true);
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(obj, sym, ctx->createNumber(3), true, false, true);

    // 不含 symbol（插入序：shown 先于 hidden）
    Value* onlyStrings = ctx->ownPropertyKeys(obj, false, false);
    EXPECT_EQ(strOf(elemOf(ctx.get(), onlyStrings, 0)), "shown");
    EXPECT_EQ(strOf(elemOf(ctx.get(), onlyStrings, 1)), "hidden");
    EXPECT_EQ(elemOf(ctx.get(), onlyStrings, 2)->type()->kind(), TypeKind::Undefined);

    // 仅可枚举 + 含 symbol（不可枚举 symbol 被过滤）
    Value* enumerable = ctx->ownPropertyKeys(obj, true, true);
    EXPECT_EQ(strOf(elemOf(ctx.get(), enumerable, 0)), "shown");
    EXPECT_EQ(elemOf(ctx.get(), enumerable, 1)->type()->kind(), TypeKind::Undefined);

    // ownEnumerableStringKeys 便捷形式
    Value* conv = ctx->ownEnumerableStringKeys(obj);
    EXPECT_EQ(strOf(elemOf(ctx.get(), conv, 0)), "shown");
    EXPECT_EQ(elemOf(ctx.get(), conv, 1)->type()->kind(), TypeKind::Undefined);

    // 非对象 → 空数组
    Value* none = ctx->ownPropertyKeys(ctx->createNumber(1), false, true);
    ASSERT_EQ(none->type()->kind(), TypeKind::Array);
    EXPECT_EQ(elemOf(ctx.get(), none, 0)->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, OwnPropertyKeysArrayIncludesLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));

    // 全键：索引升序 + length（不可枚举）
    Value* all = ctx->ownPropertyKeys(arr, false, false);
    EXPECT_EQ(strOf(elemOf(ctx.get(), all, 0)), "0");
    EXPECT_EQ(strOf(elemOf(ctx.get(), all, 1)), "1");
    EXPECT_EQ(strOf(elemOf(ctx.get(), all, 2)), "length");
    EXPECT_EQ(elemOf(ctx.get(), all, 3)->type()->kind(), TypeKind::Undefined);

    // 仅可枚举：length 被过滤
    Value* en = ctx->ownPropertyKeys(arr, true, false);
    EXPECT_EQ(strOf(elemOf(ctx.get(), en, 0)), "0");
    EXPECT_EQ(strOf(elemOf(ctx.get(), en, 1)), "1");
    EXPECT_EQ(elemOf(ctx.get(), en, 2)->type()->kind(), TypeKind::Undefined);
}

// ===== Object.prototype 其余方法 =====

TEST(ObjectBuiltinsTest, ObjectProtoIsPrototypeOf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    Value* isProto = ctx->getProperty(ctx->objectPrototype(), "isPrototypeOf");

    Value* args[1] = {obj};
    EXPECT_TRUE(boolOf(ctx->call(isProto, proto, 1, args)));
    // 自身不是自己的原型
    Value* args2[1] = {proto};
    EXPECT_FALSE(boolOf(ctx->call(isProto, proto, 1, args2)));
    // 非对象实参 → false
    Value* args3[1] = {ctx->createNumber(1)};
    EXPECT_FALSE(boolOf(ctx->call(isProto, proto, 1, args3)));
    // 无实参 → false
    EXPECT_FALSE(boolOf(ctx->call(isProto, proto, 0, nullptr)));
    // this 为 null → Exception
    EXPECT_EQ(ctx->call(isProto, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectProtoPropertyIsEnumerable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "shown", ctx->createNumber(1));
    ctx->defineProperty(obj, "hidden", ctx->createNumber(2), true, false, true);
    Value* fn = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");

    Value* args1[1] = {ctx->createString("shown")};
    EXPECT_TRUE(boolOf(ctx->call(fn, obj, 1, args1)));
    Value* args2[1] = {ctx->createString("hidden")};
    EXPECT_FALSE(boolOf(ctx->call(fn, obj, 1, args2)));
    // 原型链上的属性不算自有
    Value* args3[1] = {ctx->createString("toString")};
    EXPECT_FALSE(boolOf(ctx->call(fn, obj, 1, args3)));
    // 非对象 this → Exception
    EXPECT_EQ(ctx->call(fn, ctx->createNumber(1), 1, args1)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectProtoToLocaleStringDelegatesToToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->getProperty(ctx->objectPrototype(), "toLocaleString");
    Value* obj = ctx->createObject();
    EXPECT_EQ(strOf(ctx->call(fn, obj, 0, nullptr)), "[object Object]");
}

// ===== @@toStringTag =====

TEST(ObjectBuiltinsTest, ObjectProtoToStringUsesToStringTag) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    Value* obj = ctx->createObject();
    Value* tag = ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag);
    ctx->defineProperty(obj, tag, ctx->createString("Custom"), true, false, true);
    EXPECT_EQ(strOf(ctx->call(toStringFn, obj, 0, nullptr)), "[object Custom]");
}

// ===== 函数 length 属性 =====

TEST(ObjectBuiltinsTest, ObjectProtoToStringWrapperTags) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createPrimitiveWrapper(ctx->createNumber(1)),
                              0, nullptr)),
              "[object Number]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createPrimitiveWrapper(ctx->createString("s")),
                              0, nullptr)),
              "[object String]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createPrimitiveWrapper(ctx->createBoolean(true)),
                              0, nullptr)),
              "[object Boolean]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createPrimitiveWrapper(ctx->createSymbol("s")),
                              0, nullptr)),
              "[object Symbol]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, nullptr, 0, nullptr)), "[object Undefined]");
}

TEST(ObjectBuiltinsTest, ObjectProtoToStringTagPropagatesErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    Value* obj = ctx->createObject();
    // @@toStringTag 的 getter 抛异常 → 透传
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineAccessor(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag),
                        boom, nullptr);
    EXPECT_EQ(ctx->call(toStringFn, obj, 0, nullptr)->type()->kind(), TypeKind::Exception);

    // @@toStringTag 为对象 → ToString 抛异常（ToPrimitive 失败）→ 透传
    Value* obj2 = ctx->createObject();
    Value* badTag = ctx->createObject();
    Value* badValueOf = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("bad tag"));
        }, 0);
    ctx->defineProperty(badTag, "valueOf", badValueOf);
    ctx->defineProperty(badTag, "toString", badValueOf);
    ctx->defineProperty(obj2,
                        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag),
                        badTag, true, false, true);
    EXPECT_EQ(ctx->call(toStringFn, obj2, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, PropertyIsEnumerableOnArrayAndStringWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* idx[1] = {ctx->createString("0")};
    EXPECT_TRUE(boolOf(ctx->call(fn, arr, 1, idx)));
    Value* missing[1] = {ctx->createString("5")};
    EXPECT_FALSE(boolOf(ctx->call(fn, arr, 1, missing)));
    Value* len[1] = {ctx->createString("length")};
    EXPECT_FALSE(boolOf(ctx->call(fn, arr, 1, len)));

    Value* strObj = ctx->createPrimitiveWrapper(ctx->createString("ab"));
    EXPECT_TRUE(boolOf(ctx->call(fn, strObj, 1, idx)));
    EXPECT_FALSE(boolOf(ctx->call(fn, strObj, 1, len)));
    Value* outOfRange[1] = {ctx->createString("9")};
    EXPECT_FALSE(boolOf(ctx->call(fn, strObj, 1, outOfRange)));

    // 无实参 → false
    EXPECT_FALSE(boolOf(ctx->call(fn, arr, 0, nullptr)));
    // symbol 键
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(arr, sym, ctx->createNumber(1), true, true, true);
    Value* symArg[1] = {sym};
    EXPECT_TRUE(boolOf(ctx->call(fn, arr, 1, symArg)));
    Value* missingSym[1] = {ctx->createSymbol("other")};
    EXPECT_FALSE(boolOf(ctx->call(fn, arr, 1, missingSym)));
}

TEST(ObjectBuiltinsTest, ToLocaleStringErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->getProperty(ctx->objectPrototype(), "toLocaleString");
    // this 为 null → Exception
    EXPECT_EQ(ctx->call(fn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
    // toString 不是函数 → Exception
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "toString", ctx->createNumber(1));
    EXPECT_EQ(ctx->call(fn, obj, 0, nullptr)->type()->kind(), TypeKind::Exception);
    // toString 抛异常 → 透传
    Value* obj2 = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(obj2, "toString", boom);
    EXPECT_EQ(ctx->call(fn, obj2, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, FunctionProtoBindErrorsAndNoArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    // 非函数 → Exception
    EXPECT_EQ(ctx->call(bindFn, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    // 无参：this = undefined，无前置实参
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* bound = ctx->call(bindFn, fn, 0, nullptr);
    ASSERT_EQ(bound->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->call(bound, ctx->createObject(), 0, nullptr)->type()->kind(),
              TypeKind::Undefined);
}

// ===== 参数缺省与错误路径 =====

TEST(ObjectBuiltinsTest, ObjectStaticsWithoutArgs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 缺参按 undefined → ToObject 抛 Exception
    for (const char* name : {"keys", "values", "entries", "getPrototypeOf",
                             "getOwnPropertyNames"}) {
        EXPECT_EQ(callObjectStatic(ctx.get(), name, 0, nullptr)->type()->kind(),
                  TypeKind::Exception)
            << name;
    }
    // Object.create() 无参：proto = undefined → Exception
    EXPECT_EQ(callObjectStatic(ctx.get(), "create", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    // Object.setPrototypeOf() 无参 → Exception；只有 target 时 proto=undefined → Exception
    EXPECT_EQ(callObjectStatic(ctx.get(), "setPrototypeOf", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    Value* obj = ctx->createObject();
    Value* oneArg[1] = {obj};
    EXPECT_EQ(callObjectStatic(ctx.get(), "setPrototypeOf", 1, oneArg)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectAssignErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // target 为 null（argc 满足 minArgc）
    Value* nullTarget[2] = {ctx->createNull(), ctx->createObject()};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 2, nullTarget)->type()->kind(),
              TypeKind::Exception);

    // 源对象属性 getter 抛异常 → 透传
    Value* target = ctx->createObject();
    Value* src = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineAccessor(src, "bad", boom, nullptr, /*enumerable=*/true);
    Value* args[2] = {target, src};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 2, args)->type()->kind(),
              TypeKind::Exception);

    // 目标属性只读 → setProperty 抛异常
    Value* ro = ctx->createObject();
    ctx->defineProperty(ro, "x", ctx->createNumber(1), false, true, true);
    Value* src2 = ctx->createObject();
    ctx->setProperty(src2, "x", ctx->createNumber(2));
    Value* args2[2] = {ro, src2};
    EXPECT_EQ(callObjectStatic(ctx.get(), "assign", 2, args2)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectValuesAndEntriesPropagateGetterErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "bad", boom, nullptr, /*enumerable=*/true);
    Value* args[1] = {obj};
    EXPECT_EQ(callObjectStatic(ctx.get(), "values", 1, args)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(callObjectStatic(ctx.get(), "entries", 1, args)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectDefinePropertyDescriptorErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);

    // 描述符各字段 getter 抛异常 → 透传
    for (const char* field : {"get", "set", "enumerable", "configurable", "value",
                              "writable"}) {
        Value* desc = ctx->createObject();
        ctx->defineAccessor(desc, field, boom, nullptr);
        Value* args[3] = {obj, ctx->createString("k"), desc};
        EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args)->type()->kind(),
                  TypeKind::Exception)
            << field;
    }

    // 仅 setter 的访问器描述符
    Value* setter = ctx->createNativeFunction(returnsThis, 0);
    Value* setterDesc = ctx->createObject();
    ctx->setProperty(setterDesc, "set", setter);
    Value* args2[3] = {obj, ctx->createString("s"), setterDesc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args2), obj);

    // 仅 getter
    Value* getter = ctx->createNativeFunction(returnsThis, 0);
    Value* getterDesc = ctx->createObject();
    ctx->setProperty(getterDesc, "get", getter);
    Value* args3[3] = {obj, ctx->createString("g"), getterDesc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args3), obj);

    // value 缺省 → undefined；writable 缺省 → false
    Value* emptyDesc = ctx->createObject();
    Value* args4[3] = {obj, ctx->createString("e"), emptyDesc};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperty", 3, args4), obj);
    EXPECT_EQ(ctx->getProperty(obj, "e")->type()->kind(), TypeKind::Undefined);
}

TEST(ObjectBuiltinsTest, HasOwnPropertyOnArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* idx[1] = {ctx->createString("0")};
    EXPECT_TRUE(boolOf(ctx->call(hasOwn, arr, 1, idx)));
    Value* missing[1] = {ctx->createString("3")};
    EXPECT_FALSE(boolOf(ctx->call(hasOwn, arr, 1, missing)));
    Value* len[1] = {ctx->createString("length")};
    EXPECT_TRUE(boolOf(ctx->call(hasOwn, arr, 1, len)));
}

TEST(ObjectBuiltinsTest, PrototypeMethodsRejectNonObjectThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isProto = ctx->getProperty(ctx->objectPrototype(), "isPrototypeOf");
    Value* isEnum = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    Value* args[1] = {ctx->createObject()};
    EXPECT_EQ(ctx->call(isProto, ctx->createNumber(1), 1, args)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(isEnum, ctx->createNumber(1), 1, args)->type()->kind(),
              TypeKind::Exception);
    // 键 ToPropertyKey 失败（symbol 以外的对象 valueOf 抛异常）
    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    Value* badKey = ctx->createObject();
    ctx->defineProperty(badKey, "valueOf", boom);
    ctx->defineProperty(badKey, "toString", boom);
    Value* badArgs[1] = {badKey};
    EXPECT_EQ(ctx->call(isEnum, obj, 1, badArgs)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ApplyLengthConversionErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    // length 是对象且 ToNumber 抛异常 → 透传
    Value* badLen = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(badLen, "valueOf", boom);
    ctx->defineProperty(badLen, "toString", boom);
    Value* arrayLike = ctx->createObject();
    ctx->setProperty(arrayLike, "length", badLen);
    Value* args[2] = {ctx->createNull(), arrayLike};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args)->type()->kind(), TypeKind::Exception);

    // length = NaN → 0
    Value* nanLike = ctx->createObject();
    ctx->setProperty(nanLike, "length", ctx->createNumber(std::nan("")));
    Value* args2[2] = {ctx->createNull(), nanLike};
    Value* r = ctx->call(applyFn, fn, 2, args2);
    ASSERT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 0.0);
}

TEST(ObjectBuiltinsTest, BindWithNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* bindFn = ctx->getProperty(ctx->functionPrototype(), "bind");
    EXPECT_EQ(ctx->call(bindFn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

// ===== 覆盖补强：函数运算、symbol 属性写、popScope 守卫 =====

TEST(ObjectBuiltinsTest, FunctionValuesParticipateInArithmetic) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    Value* n = ctx->createNumber(1);
    // 函数 ToPrimitive 得源码串 → + 拼接，其余算术 NaN
    EXPECT_EQ(ctx->add(fn, n)->type()->kind(), TypeKind::String);
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(fn, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->mul(fn, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->div(fn, n))));
    // 异常透传
    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->add(exc, n), exc);
    EXPECT_EQ(ctx->sub(exc, n), exc);
    EXPECT_EQ(ctx->mul(exc, n), exc);
    EXPECT_EQ(ctx->div(exc, n), exc);
}

TEST(ObjectBuiltinsTest, SymbolPropertyAssignmentUpdatesExisting) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(obj, sym, ctx->createNumber(1), true, true, true);
    // 已有可写 symbol 属性 → 直接更新
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, sym)), 2.0);
    // 只读 symbol 属性 → Exception
    Value* ro = ctx->createObject();
    ctx->defineProperty(ro, sym, ctx->createNumber(1), false, true, true);
    EXPECT_EQ(ctx->setProperty(ro, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, PopScopeGuardsRootAndGlobal) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // current == root：popScope 无操作
    neo::core::Scope* root = ctx->root();
    ctx->popScope();
    EXPECT_EQ(ctx->current(), root);
    // 新建子 scope 后 pop 回到 root
    neo::core::Scope* child = ctx->pushNewScope(root);
    ASSERT_NE(child, nullptr);
    EXPECT_EQ(ctx->current(), child);
    ctx->popScope();
    EXPECT_EQ(ctx->current(), root);
}

TEST(ObjectBuiltinsTest, ApplyCapsHugeLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* applyFn = ctx->getProperty(ctx->functionPrototype(), "apply");
    Value* fn = ctx->createNativeFunction(argcFn, 0);
    // length 超 2^32-1 → 截断；索引 0 的 getter 抛异常使循环立即退出
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    Value* arrayLike = ctx->createObject();
    ctx->setProperty(arrayLike, "length", ctx->createNumber(1e10));
    ctx->defineAccessor(arrayLike, "0", boom, nullptr);
    Value* args[2] = {ctx->createNull(), arrayLike};
    EXPECT_EQ(ctx->call(applyFn, fn, 2, args)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ToLocaleStringPropagatesToStringLookupError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->getProperty(ctx->objectPrototype(), "toLocaleString");
    // toString 是抛异常的 getter → 查找本身透传
    Value* obj = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineAccessor(obj, "toString", boom, nullptr);
    EXPECT_EQ(ctx->call(fn, obj, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, SymbolAccessorSetPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("s");
    Value* setter = ctx->createNativeFunction(returnsThis, 0);

    // 自有 symbol 访问器：有 setter → 调用；无 setter → Exception
    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, sym, nullptr, setter);
    EXPECT_EQ(ctx->setProperty(obj, sym, ctx->createNumber(1))->type()->kind(),
              TypeKind::Number);
    Value* getterOnly = ctx->createObject();
    ctx->defineAccessor(getterOnly, sym, ctx->createNativeFunction(returnsThis, 0), nullptr);
    EXPECT_EQ(ctx->setProperty(getterOnly, sym, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);

    // 继承的 symbol 访问器：有 setter → 调用
    Value* proto = ctx->createObject();
    ctx->defineAccessor(proto, sym, nullptr, setter);
    Value* child = ctx->createObject();
    ctx->setPrototype(child, proto);
    EXPECT_EQ(ctx->setProperty(child, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Number);

    // 继承的 symbol 数据属性只读 → Exception
    Value* roProto = ctx->createObject();
    ctx->defineProperty(roProto, sym, ctx->createNumber(1), false, true, true);
    Value* roChild = ctx->createObject();
    ctx->setPrototype(roChild, roProto);
    EXPECT_EQ(ctx->setProperty(roChild, sym, ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ArrayDefinePropertyRejectsBadAttributes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    // length 不可枚举/不可配置
    EXPECT_EQ(ctx->defineProperty(arr, "length", ctx->createNumber(1), true, true, false)
                  ->type()->kind(),
              TypeKind::Exception);
    // 索引元素属性可逐元素定制
    EXPECT_EQ(numOf(ctx->defineProperty(arr, "0", ctx->createNumber(1), false, true, true)),
              1.0);
    // 合规属性：length 与索引均可正常定义
    EXPECT_EQ(ctx->defineProperty(arr, "length", ctx->createNumber(3), true, false, false)
                  ->type()->kind(),
              TypeKind::Number);
    EXPECT_EQ(ctx->defineProperty(arr, "0", ctx->createNumber(1), true, true, true)
                  ->type()->kind(),
              TypeKind::Number);
}

TEST(ObjectBuiltinsTest, HasOwnPropertyAndIsEnumerableEdgeKeys) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* isEnum = ctx->getProperty(ctx->objectPrototype(), "propertyIsEnumerable");
    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "foo", ctx->createNumber(2));

    // 数组上的非索引字符串键
    Value* foo[1] = {ctx->createString("foo")};
    EXPECT_TRUE(boolOf(ctx->call(hasOwn, arr, 1, foo)));
    EXPECT_TRUE(boolOf(ctx->call(isEnum, arr, 1, foo)));

    // 不可枚举 symbol 键 → propertyIsEnumerable false
    Value* sym = ctx->createSymbol("s");
    ctx->defineProperty(arr, sym, ctx->createNumber(1), true, false, true);
    Value* symArg[1] = {sym};
    EXPECT_FALSE(boolOf(ctx->call(isEnum, arr, 1, symArg)));

    // 非字符串包装（Number）走 properties 分支
    Value* numObj = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    EXPECT_FALSE(boolOf(ctx->call(isEnum, numObj, 1, foo)));

    // this 为 null → Exception
    EXPECT_EQ(ctx->call(isEnum, nullptr, 1, foo)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->call(hasOwn, nullptr, 1, foo)->type()->kind(), TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, IsPrototypeOfNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isProto = ctx->getProperty(ctx->objectPrototype(), "isPrototypeOf");
    Value* arg[1] = {ctx->createObject()};
    EXPECT_EQ(ctx->call(isProto, nullptr, 1, arg)->type()->kind(), TypeKind::Exception);
    // 实参为原始值 → false
    Value* prim[1] = {ctx->createNumber(1)};
    EXPECT_FALSE(boolOf(ctx->call(isProto, ctx->objectPrototype(), 1, prim)));
}

TEST(ObjectBuiltinsTest, ConstructWithNonObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // fn.prototype 被改成原始值 → 退回默认 Object.prototype（GetPrototypeFromConstructor）
    Value* fn = ctx->createNativeFunction(returnsThis, 0);
    ctx->setProperty(fn, "prototype", ctx->createNumber(1));
    Value* obj = ctx->construct(fn, 0, nullptr);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(obj), ctx->objectPrototype());

    // 构造函数显式返回对象 → 用返回值
    Value* fn3 = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* o = c->createObject();
            c->setProperty(o, "made", c->createNumber(1));
            return o;
        }, 0);
    Value* made = ctx->construct(fn3, 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(made, "made")), 1.0);
}

TEST(ObjectBuiltinsTest, SymbolPropertyDeleteAndOrder) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    Value* sym = ctx->createSymbol("s");
    // 删除不存在的 symbol → true
    EXPECT_TRUE(boolOf(ctx->deleteProperty(obj, sym)));
    // 定义后删除 → true，且从 symbolOrder 移除
    ctx->defineProperty(obj, sym, ctx->createNumber(1), true, true, true);
    EXPECT_TRUE(boolOf(ctx->deleteProperty(obj, sym)));
    auto* d = static_cast<ObjectData*>(obj->data());
    EXPECT_TRUE(d->symbolOrder.empty());
    // 不可配置 symbol → [[Delete]] 返回 false
    ctx->defineProperty(obj, sym, ctx->createNumber(1), true, true, false);
    Value* del = ctx->deleteProperty(obj, sym);
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(del));
}

TEST(ObjectBuiltinsTest, InheritedStringAccessorAndReadOnly) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* setter = ctx->createNativeFunction(returnsThis, 0);
    Value* proto = ctx->createObject();
    ctx->defineAccessor(proto, "acc", nullptr, setter);
    Value* child = ctx->createObject();
    ctx->setPrototype(child, proto);
    // 继承访问器有 setter → 调用并返回赋值
    EXPECT_EQ(ctx->setProperty(child, "acc", ctx->createNumber(1))->type()->kind(),
              TypeKind::Number);
    // 继承访问器只有 getter → Exception
    Value* getterProto = ctx->createObject();
    ctx->defineAccessor(getterProto, "g", ctx->createNativeFunction(returnsThis, 0),
                        nullptr);
    Value* child2 = ctx->createObject();
    ctx->setPrototype(child2, getterProto);
    EXPECT_EQ(ctx->setProperty(child2, "g", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    // 继承只读数据属性 → Exception
    Value* roProto = ctx->createObject();
    ctx->defineProperty(roProto, "ro", ctx->createNumber(1), false, true, true);
    Value* child3 = ctx->createObject();
    ctx->setPrototype(child3, roProto);
    EXPECT_EQ(ctx->setProperty(child3, "ro", ctx->createNumber(2))->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, IsPrototypeOfNullPrototypeChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* isProto = ctx->getProperty(ctx->objectPrototype(), "isPrototypeOf");
    // 原型链终点为 null → 循环条件 false 后返回 false
    Value* bare = ctx->createObject();
    static_cast<ObjectData*>(bare->data())->prototype = nullptr;
    Value* args[1] = {bare};
    EXPECT_FALSE(boolOf(ctx->call(isProto, ctx->objectPrototype(), 1, args)));
}

TEST(ObjectBuiltinsTest, ObjectStaticsOomPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "a", ctx->createNumber(1));
    Value* args[1] = {obj};

    // 配额耗尽：结果数组分配失败 → 返回 OOM 哨兵（ADR-031）
    alloc->setLimit(alloc->used());
    EXPECT_EQ(callObjectStatic(ctx.get(), "keys", 1, args), ctx->oomValue());
    EXPECT_EQ(callObjectStatic(ctx.get(), "values", 1, args), ctx->oomValue());
    EXPECT_EQ(callObjectStatic(ctx.get(), "entries", 1, args), ctx->oomValue());
    EXPECT_EQ(callObjectStatic(ctx.get(), "getOwnPropertyNames", 1, args),
              ctx->oomValue());
}

TEST(ObjectBuiltinsTest, FunctionHasLengthProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->createNativeFunction(sumArgs, 2);
    Value* len = ctx->getProperty(fn, "length");
    ASSERT_EQ(len->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(len), 2.0);
    // 不可写、不可枚举、可配置
    auto* d = static_cast<ObjectData*>(fn->data());
    auto it = d->properties.find("length");
    ASSERT_NE(it, d->properties.end());
    EXPECT_FALSE(it->second.writable);
    EXPECT_FALSE(it->second.enumerable);
    EXPECT_TRUE(it->second.configurable);
}

TEST(ObjectBuiltinsTest, ObjectAssignStaysStrictInSloppyCaller) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 调用方为 sloppy：普通赋值会静默失败，但 Object.assign 内部 Set 用 Throw=true。
    ctx->global()->clearFlag("strict");

    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), false, true, true);
    Value* src = ctx->createObject();
    ctx->setProperty(src, "x", ctx->createNumber(2));

    Value* args[2] = {target, src};
    Value* r = callObjectStatic(ctx.get(), "assign", 2, args);
    EXPECT_EQ(r->type()->kind(), TypeKind::Exception);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(target, "x")), 1.0);
}

TEST(ObjectBuiltinsTest, ObjectDefinePropertiesAndGetOwnPropertyDescriptors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();

    // props = { x: { value: 1, writable: false, enumerable: true, configurable: true } }
    Value* descX = ctx->createObject();
    ctx->setProperty(descX, "value", ctx->createNumber(1));
    ctx->setProperty(descX, "writable", ctx->createBoolean(false));
    ctx->setProperty(descX, "enumerable", ctx->createBoolean(true));
    ctx->setProperty(descX, "configurable", ctx->createBoolean(true));
    Value* props = ctx->createObject();
    ctx->setProperty(props, "x", descX);

    Value* args[2] = {target, props};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperties", 2, args), target);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(target, "x")), 1.0);
    // 不可写属性：写入失败（sloppy 静默）。
    ctx->setProperty(target, "x", ctx->createNumber(2));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(target, "x")), 1.0);

    // getOwnPropertyDescriptors 返回 { x: 描述符 }。
    Value* descsArgs[1] = {target};
    Value* descs = callObjectStatic(ctx.get(), "getOwnPropertyDescriptors", 1, descsArgs);
    ASSERT_EQ(descs->type()->kind(), TypeKind::Object);
    Value* xDesc = ctx->getProperty(descs, "x");
    ASSERT_EQ(xDesc->type()->kind(), TypeKind::Object);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(xDesc, "value")), 1.0);
    EXPECT_FALSE(boolOf(ctx->getProperty(xDesc, "writable")));
    EXPECT_TRUE(boolOf(ctx->getProperty(xDesc, "enumerable")));
    EXPECT_TRUE(boolOf(ctx->getProperty(xDesc, "configurable")));

    // 描述符对象非对象 → TypeError。
    Value* badProps = ctx->createObject();
    ctx->setProperty(badProps, "y", ctx->createNumber(1));
    Value* badArgs[2] = {ctx->createObject(), badProps};
    EXPECT_EQ(callObjectStatic(ctx.get(), "defineProperties", 2, badArgs)->type()->kind(),
              TypeKind::Exception);
}

TEST(ObjectBuiltinsTest, ObjectCreateAppliesPropertyDescriptors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->createObject();
    Value* desc = ctx->createObject();
    ctx->setProperty(desc, "value", ctx->createNumber(7));
    ctx->setProperty(desc, "enumerable", ctx->createBoolean(true));
    Value* props = ctx->createObject();
    ctx->setProperty(props, "x", desc);

    Value* args[2] = {proto, props};
    Value* obj = callObjectStatic(ctx.get(), "create", 2, args);
    ASSERT_EQ(obj->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(obj), proto);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 7.0);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isExtensible", 1, &obj)));
}

TEST(ObjectBuiltinsTest, SealAndFreezePlainObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, "x", ctx->createNumber(1), true, true, true);
    Value* args[1] = {obj};

    // seal：不可扩展、属性不可配置，但仍可写。
    EXPECT_EQ(callObjectStatic(ctx.get(), "seal", 1, args), obj);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isSealed", 1, args)));
    EXPECT_FALSE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, args)));
    EXPECT_FALSE(boolOf(callObjectStatic(ctx.get(), "isExtensible", 1, args)));
    ctx->setProperty(obj, "x", ctx->createNumber(2));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 2.0);
    EXPECT_EQ(ctx->defineProperty(obj, "y", ctx->createNumber(3), true, true, true)
                  ->type()->kind(),
              TypeKind::Exception);
    Value* del = ctx->deleteProperty(obj, "x");
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(del));

    // freeze：再收紧 writable。
    EXPECT_EQ(callObjectStatic(ctx.get(), "freeze", 1, args), obj);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, args)));
    ctx->setProperty(obj, "x", ctx->createNumber(9));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "x")), 2.0);
    Value* descArgs[2] = {obj, ctx->createString("x")};
    Value* desc = callObjectStatic(ctx.get(), "getOwnPropertyDescriptor", 2, descArgs);
    EXPECT_FALSE(boolOf(ctx->getProperty(desc, "writable")));
    EXPECT_FALSE(boolOf(ctx->getProperty(desc, "configurable")));

    // 原始值：isSealed/isFrozen 为 true，seal/freeze 原样返回。
    Value* prim = ctx->createNumber(1);
    Value* primArgs[1] = {prim};
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isSealed", 1, primArgs)));
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, primArgs)));
    EXPECT_EQ(callObjectStatic(ctx.get(), "freeze", 1, primArgs), prim);
}

TEST(ObjectBuiltinsTest, SealAndFreezeArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    Value* args[1] = {arr};

    // seal：索引仍可写，但不可删除/新增；length 仍可写。
    EXPECT_EQ(callObjectStatic(ctx.get(), "seal", 1, args), arr);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isSealed", 1, args)));
    EXPECT_FALSE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, args)));
    ctx->setProperty(arr, "0", ctx->createNumber(7));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(arr, "0")), 7.0);
    Value* del = ctx->deleteProperty(arr, "0");
    ASSERT_EQ(del->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(boolOf(del));
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    EXPECT_EQ(ctx->getProperty(arr, "2")->type()->kind(), TypeKind::Undefined);

    // freeze：索引不可写，length 不可写。
    EXPECT_EQ(callObjectStatic(ctx.get(), "freeze", 1, args), arr);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, args)));
    ctx->setProperty(arr, "0", ctx->createNumber(8));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(arr, "0")), 7.0);
    ctx->setProperty(arr, "length", ctx->createNumber(1));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(arr, "length")), 2.0);

    Value* descArgs[2] = {arr, ctx->createString("length")};
    Value* lenDesc = callObjectStatic(ctx.get(), "getOwnPropertyDescriptor", 2, descArgs);
    EXPECT_FALSE(boolOf(ctx->getProperty(lenDesc, "writable")));
}

TEST(ObjectBuiltinsTest, SealAndFreezeProxyForwardToTarget) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* target = ctx->createObject();
    ctx->defineProperty(target, "x", ctx->createNumber(1), true, true, true);
    Value* proxy = ctx->createProxy(target, ctx->createObject());
    Value* args[1] = {proxy};

    EXPECT_EQ(callObjectStatic(ctx.get(), "freeze", 1, args), proxy);
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, args)));
    EXPECT_TRUE(boolOf(callObjectStatic(ctx.get(), "isFrozen", 1, &target)));
    EXPECT_FALSE(boolOf(callObjectStatic(ctx.get(), "isExtensible", 1, &target)));
}
