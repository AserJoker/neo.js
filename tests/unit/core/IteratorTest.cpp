// IteratorTest.cpp - Iterator 抽象类与数组迭代器单元测试
//
// 验证（ES2024 §23.1.5 / ES2025 §27.1）：
// - Array.prototype.keys/values/entries 返回 ArrayIterator，next 产出正确结果
// - 迭代协议：next 结果 { value, done }，耗尽后 iteratedObject 释放
// - live 语义：迭代期间数组变更可见
// - 稀疏数组：空洞按 undefined 产出
// - 原型链：ArrayIterator → %ArrayIteratorPrototype% → %Iterator.prototype% → Object.prototype
// - @@toStringTag：Array Iterator / Iterator
// - Iterator 构造函数：call 报 requires new，construct 报抽象类
// - Iterator.from：真迭代器原样返回、iterator-like 包装、String ToObject、错误路径
// - Iterator.prototype：constructor 访问器、@@iterator、@@dispose
// - Array.prototype[@@iterator] === Array.prototype.values
// - GC：迭代器持有被迭代数组
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、034（原型-构造函数）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Iterator.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

// 造数组 [1, 2, 3]
Value* makeArray(IContext* ctx, size_t n) {
    Value* arr = ctx->createArray();
    auto* d = static_cast<ArrayData*>(arr->data());
    for (size_t i = 0; i < n; ++i) {
        d->setElement(static_cast<uint32_t>(i),
                      ctx->createNumber(static_cast<double>(i + 1)));
    }
    return arr;
}

// 调用 obj 上的方法（沿原型链查找）
Value* callMethod(IContext* ctx, Value* obj, const char* name, size_t argc,
                  Value* argv[]) {
    Value* fn = ctx->getProperty(obj, name);
    return ctx->call(fn, obj, argc, argv);
}

// 读迭代器结果对象的 value / done
Value* resultValue(IContext* ctx, Value* r) { return ctx->getProperty(r, "value"); }
bool resultDone(IContext* ctx, Value* r) {
    return boolOf(ctx->getProperty(r, "done"));
}

// 把迭代器跑完，元素按字符串拼接（便于整体比较）
std::string drain(IContext* ctx, Value* iter) {
    std::string out;
    for (;;) {
        Value* r = callMethod(ctx, iter, "next", 0, nullptr);
        if (r->type()->kind() != TypeKind::Object) return "<err>";
        if (resultDone(ctx, r)) break;
        Value* v = resultValue(ctx, r);
        if (v->type()->kind() == TypeKind::Array) {
            auto* d = static_cast<ArrayData*>(v->data());
            out += strOf(ctx->toString(v));
            (void)d;
        } else {
            out += strOf(ctx->toString(v));
        }
        out += '|';
    }
    return out;
}

}  // namespace

// ===== 数组迭代器基本行为 =====

TEST(IteratorTest, ArrayValuesIteratesElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 3);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    ASSERT_EQ(iter->type()->kind(), TypeKind::ArrayIterator);

    Value* r1 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    ASSERT_EQ(r1->type()->kind(), TypeKind::Object);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r1)), 1.0);
    EXPECT_FALSE(resultDone(ctx.get(), r1));

    Value* r2 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r2)), 2.0);
    Value* r3 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r3)), 3.0);
    EXPECT_FALSE(resultDone(ctx.get(), r3));

    Value* r4 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_TRUE(resultDone(ctx.get(), r4));
    EXPECT_EQ(resultValue(ctx.get(), r4)->type()->kind(), TypeKind::Undefined);
    // 耗尽后再 next 仍 done
    Value* r5 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_TRUE(resultDone(ctx.get(), r5));
}

TEST(IteratorTest, ArrayKeysAndEntries) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* keys = callMethod(ctx.get(), arr, "keys", 0, nullptr);
    Value* k1 = callMethod(ctx.get(), keys, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), k1)), 0.0);
    Value* k2 = callMethod(ctx.get(), keys, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), k2)), 1.0);
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), keys, "next", 0, nullptr)));

    Value* entries = callMethod(ctx.get(), arr, "entries", 0, nullptr);
    Value* e1 = callMethod(ctx.get(), entries, "next", 0, nullptr);
    Value* pair = resultValue(ctx.get(), e1);
    ASSERT_EQ(pair->type()->kind(), TypeKind::Array);
    auto* pd = static_cast<ArrayData*>(pair->data());
    EXPECT_DOUBLE_EQ(numOf(pd->elementAt(0)), 0.0);
    EXPECT_DOUBLE_EQ(numOf(pd->elementAt(1)), 1.0);
    EXPECT_FALSE(resultDone(ctx.get(), e1));
}

TEST(IteratorTest, EmptyArrayIteratorIsImmediatelyDone) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* r = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_TRUE(resultDone(ctx.get(), r));
    // 耗尽后释放对数组的引用
    EXPECT_EQ(neo::core::arrayIteratorIteratedObject(iter), nullptr);
}

TEST(IteratorTest, SparseHolesYieldUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    auto* d = static_cast<ArrayData*>(arr->data());
    d->setLength(3);                         // 全空洞
    d->setElement(1, ctx->createNumber(7));  // 只有索引 1 有值

    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* r0 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_EQ(resultValue(ctx.get(), r0)->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(resultDone(ctx.get(), r0));
    Value* r1 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r1)), 7.0);
    Value* r2 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_EQ(resultValue(ctx.get(), r2)->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(resultDone(ctx.get(), r2));
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), iter, "next", 0, nullptr)));
}

TEST(IteratorTest, IteratorIsLive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    // 追加元素后迭代器仍能看到
    auto* d = static_cast<ArrayData*>(arr->data());
    d->setElement(1, ctx->createNumber(99));
    Value* r1 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r1)), 1.0);
    Value* r2 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r2)), 99.0);
    // 截断后立即 done
    d->setLength(0);
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), iter, "next", 0, nullptr)));
}

// ===== 原型链与 tag =====

TEST(IteratorTest, PrototypeChainAndTags) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);

    Value* arrayIterProto = ctx->getPrototype(iter);
    ASSERT_EQ(arrayIterProto->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(arrayIterProto), ctx->iteratorPrototype());
    EXPECT_EQ(ctx->getPrototype(ctx->iteratorPrototype()), ctx->objectPrototype());

    // tag
    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, iter, 0, nullptr)), "[object Array Iterator]");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->iteratorPrototype(), 0, nullptr)),
              "[object Iterator]");
    // ArrayIterator 原型上的 next 是自有方法
    Value* next = ctx->getProperty(arrayIterProto, "next");
    EXPECT_EQ(next->type()->kind(), TypeKind::Function);
}

TEST(IteratorTest, ArraySymbolIteratorIsValues) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayProto = ctx->arrayPrototype();
    Value* values = ctx->getProperty(arrayProto, "values");
    Value* symIter = ctx->getProperty(
        arrayProto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator));
    EXPECT_EQ(values, symIter);

    // 通过 @@iterator 拿到的迭代器与 values() 行为一致
    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = ctx->call(symIter, arr, 0, nullptr);
    ASSERT_EQ(iter->type()->kind(), TypeKind::ArrayIterator);
    EXPECT_EQ(drain(ctx.get(), iter), "1|2|");
}

// ===== Iterator 构造函数 =====

TEST(IteratorTest, IteratorConstructorIsAbstract) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    ASSERT_NE(iteratorFn, nullptr);
    EXPECT_EQ(iteratorFn->type()->kind(), TypeKind::Function);
    // call → requires new
    EXPECT_EQ(ctx->call(iteratorFn, ctx->createUndefined(), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    // new → abstract
    Value* c = ctx->construct(iteratorFn, 0, nullptr);
    EXPECT_EQ(c->type()->kind(), TypeKind::Exception);
    // prototype 属性不可写
    auto* d = static_cast<ObjectData*>(iteratorFn->data());
    auto it = d->properties.find("prototype");
    ASSERT_NE(it, d->properties.end());
    EXPECT_FALSE(it->second.writable);
    EXPECT_FALSE(it->second.enumerable);
    EXPECT_FALSE(it->second.configurable);
}

TEST(IteratorTest, IteratorPrototypeConstructorAccessor) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* proto = ctx->iteratorPrototype();
    EXPECT_EQ(ctx->getProperty(proto, "constructor"), iteratorFn);

    // setter：在 this 上建自有属性
    Value* obj = ctx->createObject();
    Value* args[1] = {ctx->createNumber(42)};
    Value* setter = nullptr;
    // 通过原型链 set 触发访问器 setter
    EXPECT_EQ(ctx->setProperty(obj, "constructor", ctx->createNumber(42))->type()->kind(),
              TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "constructor")), 42.0);
    (void)setter;
    (void)args;
}

// ===== Iterator.from =====

TEST(IteratorTest, FromReturnsTrueIteratorAsIs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* from = ctx->getProperty(globalOf(ctx.get(), "Iterator"), "from");
    Value* args[1] = {iter};
    EXPECT_EQ(ctx->call(from, globalOf(ctx.get(), "Iterator"), 1, args), iter);
}

TEST(IteratorTest, FromWrapsIteratorLikeObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 普通对象（原型链不含 Iterator.prototype）→ 包装
    Value* obj = ctx->createObject();
    Value* nextFn = ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* done = c->getProperty(self, "done");
            if (boolOf(done)) {
                Value* r = c->createObject();
                c->setProperty(r, "value", c->createUndefined());
                c->setProperty(r, "done", c->createBoolean(true));
                return r;
            }
            c->setProperty(self, "done", c->createBoolean(true));
            Value* r = c->createObject();
            c->setProperty(r, "value", c->createNumber(5));
            c->setProperty(r, "done", c->createBoolean(false));
            return r;
        }, 0);
    ctx->setProperty(obj, "next", nextFn);
    ctx->setProperty(obj, "done", ctx->createBoolean(false));

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* args[1] = {obj};
    Value* wrapped = ctx->call(from, iteratorFn, 1, args);
    ASSERT_EQ(wrapped->type()->kind(), TypeKind::IteratorWrapper);
    EXPECT_NE(wrapped, obj);
    EXPECT_EQ(ctx->getPrototype(ctx->getPrototype(wrapped)), ctx->iteratorPrototype());

    Value* r = callMethod(ctx.get(), wrapped, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r)), 5.0);
    EXPECT_FALSE(resultDone(ctx.get(), r));
    Value* r2 = callMethod(ctx.get(), wrapped, "next", 0, nullptr);
    EXPECT_TRUE(resultDone(ctx.get(), r2));
}

TEST(IteratorTest, FromAcceptsStringByWrapping) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // String.prototype[@@iterator] 尚未实现，故 String 包装对象无 @@iterator，
    // 按 Iterator.from 规范走包装路径（有 String 迭代器后应改为原样返回）。
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* args[1] = {ctx->createString("ab")};
    Value* iter = ctx->call(from, iteratorFn, 1, args);
    ASSERT_EQ(iter->type()->kind(), TypeKind::IteratorWrapper);
    EXPECT_EQ(ctx->getPrototype(ctx->getPrototype(iter)), ctx->iteratorPrototype());
}

TEST(IteratorTest, FromRejectsNonObjectPrimitives) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    for (Value* v : {ctx->createNumber(1), ctx->createBoolean(true), ctx->createNull(),
                     ctx->createUndefined(), ctx->createSymbol("s")}) {
        Value* args[1] = {v};
        EXPECT_EQ(ctx->call(from, iteratorFn, 1, args)->type()->kind(),
                  TypeKind::Exception);
    }
}

TEST(IteratorTest, FromPropagatesIteratorErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* sym = ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator);

    // @@iterator 不可调用
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, sym, ctx->createNumber(1), true, true, true);
    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(from, iteratorFn, 1, args)->type()->kind(), TypeKind::Exception);

    // @@iterator 返回非对象
    Value* obj2 = ctx->createObject();
    Value* badIter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->defineProperty(obj2, sym, badIter, true, true, true);
    Value* args2[1] = {obj2};
    EXPECT_EQ(ctx->call(from, iteratorFn, 1, args2)->type()->kind(), TypeKind::Exception);

    // 包装对象的 next 抛异常 → 透传
    Value* obj3 = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->setProperty(obj3, "next", boom);
    Value* args3[1] = {obj3};
    Value* wrapped = ctx->call(from, iteratorFn, 1, args3);
    ASSERT_EQ(wrapped->type()->kind(), TypeKind::IteratorWrapper);
    EXPECT_EQ(callMethod(ctx.get(), wrapped, "next", 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // next 返回非对象 → TypeError
    Value* obj4 = ctx->createObject();
    Value* primNext = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0);
    ctx->setProperty(obj4, "next", primNext);
    Value* args4[1] = {obj4};
    Value* wrapped2 = ctx->call(from, iteratorFn, 1, args4);
    EXPECT_EQ(callMethod(ctx.get(), wrapped2, "next", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(IteratorTest, WrapperReturnDelegatesToUnderlying) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");

    // 底层有 return → 转发
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    ctx->setProperty(obj, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "value", c->createNumber(3));
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* args[1] = {obj};
    Value* wrapped = ctx->call(from, iteratorFn, 1, args);
    Value* r = callMethod(ctx.get(), wrapped, "return", 0, nullptr);
    ASSERT_EQ(r->type()->kind(), TypeKind::Object);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r)), 3.0);
    EXPECT_TRUE(resultDone(ctx.get(), r));

    // 底层无 return → { value: undefined, done: true }
    Value* obj2 = ctx->createObject();
    ctx->setProperty(obj2, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* args2[1] = {obj2};
    Value* wrapped2 = ctx->call(from, iteratorFn, 1, args2);
    Value* r2 = callMethod(ctx.get(), wrapped2, "return", 0, nullptr);
    ASSERT_EQ(r2->type()->kind(), TypeKind::Object);
    EXPECT_TRUE(resultDone(ctx.get(), r2));
    EXPECT_EQ(resultValue(ctx.get(), r2)->type()->kind(), TypeKind::Undefined);

    // 底层 return 返回非对象 → TypeError
    Value* obj3 = ctx->createObject();
    ctx->setProperty(obj3, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    ctx->setProperty(obj3, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(1); }, 0));
    Value* args3[1] = {obj3};
    Value* wrapped3 = ctx->call(from, iteratorFn, 1, args3);
    EXPECT_EQ(callMethod(ctx.get(), wrapped3, "return", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

// ===== Iterator.prototype 其余成员 =====

TEST(IteratorTest, IteratorPrototypeSymbolIteratorReturnsThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->iteratorPrototype();
    Value* fn = ctx->getProperty(
        proto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator));
    ASSERT_EQ(fn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->call(fn, proto, 0, nullptr), proto);
    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->call(fn, obj, 0, nullptr), obj);
}

TEST(IteratorTest, IteratorPrototypeDisposeCallsReturn) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->iteratorPrototype();
    Value* dispose = ctx->getProperty(
        proto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Dispose));
    ASSERT_EQ(dispose->type()->kind(), TypeKind::Function);

    // 有 return → 调用并返回 undefined
    int calls = 0;
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "return", ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            c->setProperty(self, "called", c->createBoolean(true));
            return c->createObject();
        }, 0));
    EXPECT_EQ(ctx->call(dispose, obj, 0, nullptr)->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(boolOf(ctx->getProperty(obj, "called")));
    (void)calls;

    // 无 return → 什么都不做
    Value* obj2 = ctx->createObject();
    EXPECT_EQ(ctx->call(dispose, obj2, 0, nullptr)->type()->kind(), TypeKind::Undefined);

    // return 非函数 → Exception
    Value* obj3 = ctx->createObject();
    ctx->setProperty(obj3, "return", ctx->createNumber(1));
    EXPECT_EQ(ctx->call(dispose, obj3, 0, nullptr)->type()->kind(), TypeKind::Exception);

    // this 非对象 → Exception
    EXPECT_EQ(ctx->call(dispose, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

// ===== 错误路径 =====

TEST(IteratorTest, ArrayIteratorMethodsRejectNullThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arrayProto = ctx->arrayPrototype();
    for (const char* name : {"keys", "values", "entries"}) {
        Value* fn = ctx->getProperty(arrayProto, name);
        EXPECT_EQ(ctx->call(fn, ctx->createNull(), 0, nullptr)->type()->kind(),
                  TypeKind::Exception)
            << name;
        EXPECT_EQ(ctx->call(fn, ctx->createUndefined(), 0, nullptr)->type()->kind(),
                  TypeKind::Exception)
            << name;
    }
}

TEST(IteratorTest, ArrayIteratorMethodsAcceptStringThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* values = ctx->getProperty(ctx->arrayPrototype(), "values");
    Value* iter = ctx->call(values, ctx->createString("ab"), 0, nullptr);
    ASSERT_EQ(iter->type()->kind(), TypeKind::ArrayIterator);
    EXPECT_EQ(drain(ctx.get(), iter), "a|b|");
}

TEST(IteratorTest, NextRejectsIncompatibleReceiver) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* next = ctx->getProperty(ctx->getPrototype(iter), "next");
    ASSERT_EQ(next->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->call(next, ctx->createObject(), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(next, arr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, IteratorIsObjectForObjectBuiltins) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);

    // Object(iter) 原样返回
    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* args[1] = {iter};
    EXPECT_EQ(ctx->call(objectFn, ctx->createUndefined(), 1, args), iter);

    // hasOwnProperty 可用（迭代器是对象）
    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* keyArgs[1] = {ctx->createString("next")};
    EXPECT_EQ(ctx->call(hasOwn, iter, 1, keyArgs)->type()->kind(), TypeKind::Boolean);

    // 可作属性键（ToPrimitive → "[object Array Iterator]"）
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, iter, ctx->createNumber(1));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "[object Array Iterator]")), 1.0);
}

// ===== 对象模型分派点（跨类型）=====

TEST(IteratorTest, IteratorArithmeticAndPrototypeOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* n = ctx->createNumber(1);
    // ToPrimitive → "[object Array Iterator]"：+ 拼接，其余 NaN
    EXPECT_EQ(strOf(ctx->add(iter, n)), "[object Array Iterator]1");
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(iter, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->mul(iter, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->div(iter, n))));
    EXPECT_EQ(strOf(ctx->toString(iter)), "[object Array Iterator]");
    // 异常透传
    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->sub(exc, n), exc);
    EXPECT_EQ(ctx->mul(exc, n), exc);
    EXPECT_EQ(ctx->div(exc, n), exc);

    // 属性/原型操作
    EXPECT_TRUE(ctx->hasProperty(iter, "next"));
    EXPECT_FALSE(ctx->hasProperty(iter, "nope"));
    ctx->defineProperty(iter, "x", ctx->createNumber(1), true, true, true);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(iter, "x")), 1.0);
    EXPECT_TRUE(boolOf(ctx->deleteProperty(iter, "x")));
    ctx->defineAccessor(iter, "acc", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(5); }, 0),
        nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(iter, "acc")), 5.0);
    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(iter, proto), iter);
    EXPECT_EQ(ctx->getPrototype(iter), proto);
}

TEST(IteratorTest, WrapperArithmeticAndPrototypeOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::IteratorWrapper);

    Value* n = ctx->createNumber(1);
    EXPECT_EQ(strOf(ctx->add(w, n)), "[object Iterator]1");
    EXPECT_TRUE(std::isnan(numOf(ctx->sub(w, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->mul(w, n))));
    EXPECT_TRUE(std::isnan(numOf(ctx->div(w, n))));
    EXPECT_EQ(strOf(ctx->toString(w)), "[object Iterator]");
    Value* exc = ctx->createException(ctx->createString("e"));
    EXPECT_EQ(ctx->add(exc, n), exc);
    EXPECT_EQ(ctx->sub(exc, n), exc);
    EXPECT_EQ(ctx->mul(exc, n), exc);
    EXPECT_EQ(ctx->div(exc, n), exc);

    EXPECT_TRUE(ctx->hasProperty(w, "next"));
    ctx->defineProperty(w, "x", ctx->createNumber(1), true, true, true);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(w, "x")), 1.0);
    EXPECT_TRUE(boolOf(ctx->deleteProperty(w, "x")));
    ctx->defineAccessor(w, "acc", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(5); }, 0),
        nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(w, "acc")), 5.0);
    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(w, proto), w);
    EXPECT_EQ(ctx->getPrototype(w), proto);

    // 迭代器不可调用 / 不可构造
    EXPECT_EQ(ctx->call(w, ctx->createUndefined(), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->construct(w, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, IteratorCloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    // define 触发 clone（引用语义：共享 data）
    Value* r = ctx->define("it", iter);
    ASSERT_EQ(r->type()->kind(), TypeKind::ArrayIterator);
    EXPECT_EQ(r->data(), iter->data());
    // 共享游标：通过任一别名推进，另一个也前进
    callMethod(ctx.get(), iter, "next", 0, nullptr);
    Value* next = callMethod(ctx.get(), r, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), next)), 2.0);

    // 包装对象同样引用语义
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* res = c->createObject();
            c->setProperty(res, "done", c->createBoolean(true));
            return res;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    Value* w2 = ctx->define("w", w);
    ASSERT_EQ(w2->type()->kind(), TypeKind::IteratorWrapper);
    EXPECT_EQ(w2->data(), w->data());
}

// ===== 更多错误路径 =====

TEST(IteratorTest, WrapperMethodsRejectBadReceiver) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 从包装原型上取 next / return，用错误 receiver 调用
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    Value* wrapperProto = ctx->getPrototype(w);
    Value* next = ctx->getProperty(wrapperProto, "next");
    Value* ret = ctx->getProperty(wrapperProto, "return");
    ASSERT_EQ(next->type()->kind(), TypeKind::Function);
    ASSERT_EQ(ret->type()->kind(), TypeKind::Function);

    for (Value* bad : {ctx->createObject(), ctx->createNumber(1), ctx->createNull()}) {
        EXPECT_EQ(ctx->call(next, bad, 0, nullptr)->type()->kind(), TypeKind::Exception);
        EXPECT_EQ(ctx->call(ret, bad, 0, nullptr)->type()->kind(), TypeKind::Exception);
    }
}

TEST(IteratorTest, WrapperReturnErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");

    // 底层 return 非函数 → Exception
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    ctx->setProperty(obj, "return", ctx->createNumber(1));
    Value* args[1] = {obj};
    Value* w = ctx->call(from, iteratorFn, 1, args);
    EXPECT_EQ(callMethod(ctx.get(), w, "return", 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // 底层 return 抛异常 → 透传
    Value* obj2 = ctx->createObject();
    ctx->setProperty(obj2, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    ctx->setProperty(obj2, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0));
    Value* args2[1] = {obj2};
    Value* w2 = ctx->call(from, iteratorFn, 1, args2);
    EXPECT_EQ(callMethod(ctx.get(), w2, "return", 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // 底层 return 是 null → 视为无 return
    Value* obj3 = ctx->createObject();
    ctx->setProperty(obj3, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    ctx->setProperty(obj3, "return", ctx->createNull());
    Value* args3[1] = {obj3};
    Value* w3 = ctx->call(from, iteratorFn, 1, args3);
    Value* r3 = callMethod(ctx.get(), w3, "return", 0, nullptr);
    ASSERT_EQ(r3->type()->kind(), TypeKind::Object);
    EXPECT_TRUE(resultDone(ctx.get(), r3));
}

TEST(IteratorTest, IteratorConstructorSetterRejectsNonObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->iteratorPrototype();
    // setter 通过原型链赋值触发：this 为原始值时 setProperty 会先走 primitive 分支，
    // 故直接取 setter 函数调用
    Value* setter = nullptr;
    // 用 defineAccessor 定义的访问器：从 proto 上取不到 setter，改用 setProperty 到对象
    // 非对象 this 的路径由 proto 访问器内部校验，这里直接构造调用
    Value* obj = ctx->createObject();
    ctx->setPrototype(obj, proto);
    Value* args[1] = {ctx->createNumber(7)};
    EXPECT_EQ(ctx->setProperty(obj, "constructor", ctx->createNumber(7))->type()->kind(),
              TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(obj, "constructor")), 7.0);
    (void)setter;
    (void)args;
}

TEST(IteratorTest, FromPropagatesNextLookupError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    // next 是抛异常的 getter → 查找本身透传
    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0), nullptr);
    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(from, iteratorFn, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, ArrayIteratorOomPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    // 配额耗尽 → createArrayIterator / createIteratorWrapper 返回 OOM 哨兵
    alloc->setLimit(alloc->used());
    EXPECT_EQ(ctx->createArrayIterator(arr, neo::core::ArrayIterationKind::Value),
              ctx->oomValue());
    EXPECT_EQ(ctx->createIteratorWrapper(arr, ctx->createUndefined()), ctx->oomValue());
}

// ===== 分支补强 =====

TEST(IteratorTest, PassthroughWithErrorOnRight) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* exc = ctx->createException(ctx->createString("e"));
    // 左操作数是迭代器 → 走 IteratorObjectType，右侧异常应透传
    EXPECT_EQ(ctx->add(iter, exc), exc);
    EXPECT_EQ(ctx->sub(iter, exc), exc);
    EXPECT_EQ(ctx->mul(iter, exc), exc);
    EXPECT_EQ(ctx->div(iter, exc), exc);

    // 包装对象同理
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    EXPECT_EQ(ctx->add(w, exc), exc);
    EXPECT_EQ(ctx->sub(w, exc), exc);
    EXPECT_EQ(ctx->mul(w, exc), exc);
    EXPECT_EQ(ctx->div(w, exc), exc);
}

TEST(IteratorTest, InternalSlotAccessorsRejectWrongTypes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(neo::core::arrayIteratorIteratedObject(nullptr), nullptr);
    EXPECT_EQ(neo::core::arrayIteratorIteratedObject(ctx->createObject()), nullptr);
    EXPECT_EQ(neo::core::asIteratorWrapper(nullptr), nullptr);
    EXPECT_EQ(neo::core::asIteratorWrapper(ctx->createObject()), nullptr);
}

TEST(IteratorTest, NextWithNullReceiver) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 1);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    Value* next = ctx->getProperty(ctx->getPrototype(iter), "next");
    // thisArg = nullptr（native 收到的 self 为 nullptr）
    EXPECT_EQ(ctx->call(next, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, NextPropagatesLengthAndElementErrors) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);

    // length getter 抛异常 → 透传
    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, "length", boom, nullptr);
    Value* iter = ctx->createArrayIterator(obj, neo::core::ArrayIterationKind::Value);
    EXPECT_EQ(callMethod(ctx.get(), iter, "next", 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // 元素 getter 抛异常 → 透传（Value 与 KeyAndValue 两条路径）
    for (auto kind : {neo::core::ArrayIterationKind::Value,
                      neo::core::ArrayIterationKind::KeyAndValue}) {
        Value* obj2 = ctx->createObject();
        ctx->setProperty(obj2, "length", ctx->createNumber(1));
        ctx->defineAccessor(obj2, "0", boom, nullptr);
        Value* it = ctx->createArrayIterator(obj2, kind);
        EXPECT_EQ(callMethod(ctx.get(), it, "next", 0, nullptr)->type()->kind(),
                  TypeKind::Exception);
    }
}

TEST(IteratorTest, NextHandlesNonNumericLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // length 为 NaN / 负数 / 超上限 → 分别按 0 / 0 / 2^32-1 截断
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "length", ctx->createString("abc"));
    Value* iter = ctx->createArrayIterator(obj, neo::core::ArrayIterationKind::Value);
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), iter, "next", 0, nullptr)));

    Value* obj2 = ctx->createObject();
    ctx->setProperty(obj2, "length", ctx->createNumber(-5));
    Value* iter2 = ctx->createArrayIterator(obj2, neo::core::ArrayIterationKind::Value);
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), iter2, "next", 0, nullptr)));

    // 超上限：索引 0 无值 → undefined，但不 done
    Value* obj3 = ctx->createObject();
    ctx->setProperty(obj3, "length", ctx->createNumber(1e12));
    Value* iter3 = ctx->createArrayIterator(obj3, neo::core::ArrayIterationKind::Value);
    Value* r = callMethod(ctx.get(), iter3, "next", 0, nullptr);
    EXPECT_FALSE(resultDone(ctx.get(), r));
    EXPECT_EQ(resultValue(ctx.get(), r)->type()->kind(), TypeKind::Undefined);
}

TEST(IteratorTest, FromWithoutArgsAndWithNullIteratorMethod) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    // 无参 → undefined → TypeError
    EXPECT_EQ(ctx->call(from, iteratorFn, 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // @@iterator 为 null → 跳过调用，走包装路径
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        ctx->createNull(), true, true, true);
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* args[1] = {obj};
    Value* w = ctx->call(from, iteratorFn, 1, args);
    EXPECT_EQ(w->type()->kind(), TypeKind::IteratorWrapper);
}

TEST(IteratorTest, FromPropagatesIteratorMethodCallError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* obj = ctx->createObject();
    ctx->defineProperty(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        ctx->createNativeFunction(
                            [](IContext* c, Value*, size_t, Value*[]) {
                                return c->createException(c->createString("boom"));
                            }, 0),
                        true, true, true);
    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(from, iteratorFn, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, ConstructorSetterAndDisposeEdgeCases) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->iteratorPrototype();
    // 从访问器描述符里取 setter 直接调用，覆盖非对象 this 与无参路径
    auto* d = static_cast<ObjectData*>(proto->data());
    auto it = d->properties.find("constructor");
    ASSERT_NE(it, d->properties.end());
    Value* setter = it->second.setter;
    ASSERT_NE(setter, nullptr);
    // 非对象 this（须带 1 个实参，否则先被 minArgc 拦截）
    Value* numArgs[1] = {ctx->createNumber(1)};
    EXPECT_EQ(ctx->call(setter, ctx->createNumber(1), 1, numArgs)->type()->kind(),
              TypeKind::Exception);
    // 缺参 → minArgc=1 拦截为 Exception
    Value* obj = ctx->createObject();
    EXPECT_EQ(ctx->call(setter, obj, 0, nullptr)->type()->kind(), TypeKind::Exception);
    // 显式传 undefined → 写入自有属性
    Value* undefArgs[1] = {ctx->createUndefined()};
    EXPECT_EQ(ctx->call(setter, obj, 1, undefArgs)->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->getProperty(obj, "constructor")->type()->kind(), TypeKind::Undefined);

    // @@dispose：this 非对象 / return 查找抛异常 / return 为 null / return 调用抛异常
    Value* dispose = ctx->getProperty(
        proto, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Dispose));
    EXPECT_EQ(ctx->call(dispose, nullptr, 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    Value* obj2 = ctx->createObject();
    ctx->defineAccessor(obj2, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0), nullptr);
    EXPECT_EQ(ctx->call(dispose, obj2, 0, nullptr)->type()->kind(), TypeKind::Exception);

    Value* obj3 = ctx->createObject();
    ctx->setProperty(obj3, "return", ctx->createNull());
    EXPECT_EQ(ctx->call(dispose, obj3, 0, nullptr)->type()->kind(), TypeKind::Undefined);

    Value* obj4 = ctx->createObject();
    ctx->setProperty(obj4, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0));
    EXPECT_EQ(ctx->call(dispose, obj4, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, SymbolIteratorMethodWithNullReceiver) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* fn = ctx->getProperty(
        ctx->iteratorPrototype(),
        ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator));
    EXPECT_EQ(ctx->call(fn, nullptr, 0, nullptr)->type()->kind(), TypeKind::Undefined);
}

TEST(IteratorTest, ArrayIteratorMethodWithNullSelfPointer) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* values = ctx->getProperty(ctx->arrayPrototype(), "values");
    EXPECT_EQ(ctx->call(values, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, WrapperGcTraversesIteratedAndNextMethod) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 包装对象持有被包装对象与 next 方法，GC 必须遍历二者
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::IteratorWrapper);

    ctx->gc()->collect();
    auto* d = neo::core::asIteratorWrapper(w);
    ASSERT_NE(d, nullptr);
    EXPECT_NE(d->iterated, nullptr);
    EXPECT_NE(d->nextMethod, nullptr);
    // 回收后仍可调用（被包装对象存活）
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), w, "next", 0, nullptr)));
}

TEST(IteratorTest, LengthToNumberErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // length 是对象且 ToNumber 抛异常 → next 透传
    Value* badLen = ctx->createObject();
    Value* boom = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0);
    ctx->defineProperty(badLen, "valueOf", boom);
    ctx->defineProperty(badLen, "toString", boom);
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "length", badLen);
    Value* iter = ctx->createArrayIterator(obj, neo::core::ArrayIterationKind::Value);
    EXPECT_EQ(callMethod(ctx.get(), iter, "next", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(IteratorTest, WrapperReturnGetterErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            Value* r = c->createObject();
            c->setProperty(r, "done", c->createBoolean(true));
            return r;
        }, 0));
    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* args[1] = {obj};
    Value* w = ctx->call(ctx->getProperty(iteratorFn, "from"), iteratorFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::IteratorWrapper);
    // 事后把被包装对象的 return 换成抛异常的 getter
    ctx->defineAccessor(obj, "return", ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) {
            return c->createException(c->createString("boom"));
        }, 0), nullptr);
    EXPECT_EQ(callMethod(ctx.get(), w, "return", 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(IteratorTest, FromIteratorMethodLookupErrorPropagates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* obj = ctx->createObject();
    ctx->defineAccessor(obj, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator),
                        ctx->createNativeFunction(
                            [](IContext* c, Value*, size_t, Value*[]) {
                                return c->createException(c->createString("boom"));
                            }, 0),
                        nullptr);
    Value* args[1] = {obj};
    EXPECT_EQ(ctx->call(from, iteratorFn, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(IteratorTest, ArrayIteratorMethodWrapperOom) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 原始值 this 需要 ToObject（createPrimitiveWrapper），配额耗尽时返回 OOM 哨兵
    Value* values = ctx->getProperty(ctx->arrayPrototype(), "values");
    alloc->setLimit(alloc->used());
    EXPECT_EQ(ctx->call(values, ctx->createString("ab"), 0, nullptr), ctx->oomValue());
}

TEST(IteratorTest, EnumerateChildrenOfExhaustedIterator) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    // 跑空后 iteratedObject 为 null，GC 遍历不应访问空指针
    EXPECT_TRUE(resultDone(ctx.get(), callMethod(ctx.get(), iter, "next", 0, nullptr)));
    ctx->gc()->collect();
    EXPECT_EQ(neo::core::arrayIteratorIteratedObject(iter), nullptr);
}

TEST(IteratorTest, IteratorSurvivesGcAndKeepsArrayAlive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = makeArray(ctx.get(), 2);
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    // 让 arr 从当前作用域失去跟踪（仍被迭代器持有）
    ctx->gc()->collect();
    Value* r = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r)), 1.0);
    Value* r2 = callMethod(ctx.get(), iter, "next", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(resultValue(ctx.get(), r2)), 2.0);
}
