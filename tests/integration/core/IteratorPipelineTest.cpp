// IteratorPipelineTest.cpp - 迭代器协议端到端集成测试
//
// 端到端验证"数组 → 迭代器 → next 产出 → 与对象模型/GC 协作"全链路
// （真实依赖不 mock）。
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、027（集成测试）

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

#include <string>

namespace {

using neo::core::ArrayData;
using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::NumberData;
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

Value* callMethod(IContext* ctx, Value* obj, const char* name, size_t argc,
                  Value* argv[]) {
    return ctx->call(ctx->getProperty(obj, name), obj, argc, argv);
}

// 用 for-of 的等价流程把迭代器跑完：反复 next 直到 done
std::string drain(IContext* ctx, Value* iter) {
    std::string out;
    for (;;) {
        Value* r = callMethod(ctx, iter, "next", 0, nullptr);
        if (r->type()->kind() != TypeKind::Object) return "<err>";
        if (boolOf(ctx->getProperty(r, "done"))) break;
        out += strOf(ctx->toString(ctx->getProperty(r, "value")));
        out += '|';
    }
    return out;
}

// 模拟 for-of 的收尾：调 return（若存在）
Value* closeIterator(IContext* ctx, Value* iter) {
    Value* ret = ctx->getProperty(iter, "return");
    if (ret->type()->kind() != TypeKind::Function) return nullptr;
    return ctx->call(ret, iter, 0, nullptr);
}

}  // namespace

// ============ 场景 1：数组迭代器全流程 ============

TEST(IteratorPipelineTest, ArrayIterationEndToEnd) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    auto* d = static_cast<ArrayData*>(arr->data());
    for (uint32_t i = 0; i < 4; ++i) {
        d->setElement(i, ctx->createString(std::string(1, static_cast<char>('a' + i))));
    }

    Value* symIter = ctx->getProperty(
        arr, ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::Iterator));
    ASSERT_EQ(symIter->type()->kind(), TypeKind::Function);
    Value* iter = ctx->call(symIter, arr, 0, nullptr);
    ASSERT_EQ(iter->type()->kind(), TypeKind::ArrayIterator);
    EXPECT_EQ(drain(ctx.get(), iter), "a|b|c|d|");

    // keys / entries 复用同一数组
    Value* keys = callMethod(ctx.get(), arr, "keys", 0, nullptr);
    EXPECT_EQ(drain(ctx.get(), keys), "0|1|2|3|");
    Value* entries = callMethod(ctx.get(), arr, "entries", 0, nullptr);
    Value* first = callMethod(ctx.get(), entries, "next", 0, nullptr);
    Value* pair = ctx->getProperty(first, "value");
    ASSERT_EQ(pair->type()->kind(), TypeKind::Array);
    auto* pd = static_cast<ArrayData*>(pair->data());
    EXPECT_DOUBLE_EQ(numOf(pd->elementAt(0)), 0.0);
    EXPECT_EQ(strOf(pd->elementAt(1)), "a");
}

// ============ 场景 2：迭代期间修改数组（live 语义） ============

TEST(IteratorPipelineTest, LiveIterationReflectsMutation) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    auto* d = static_cast<ArrayData*>(arr->data());
    d->setElement(0, ctx->createNumber(1));
    d->setElement(1, ctx->createNumber(2));

    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(
                        callMethod(ctx.get(), iter, "next", 0, nullptr), "value")),
                     1.0);
    // 迭代中途追加元素 → 后续 next 能看到
    d->setElement(2, ctx->createNumber(3));
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(
                        callMethod(ctx.get(), iter, "next", 0, nullptr), "value")),
                     2.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(
                        callMethod(ctx.get(), iter, "next", 0, nullptr), "value")),
                     3.0);
    EXPECT_TRUE(boolOf(ctx->getProperty(
        callMethod(ctx.get(), iter, "next", 0, nullptr), "done")));
}

// ============ 场景 3：Iterator.from 提升自定义迭代器 ============

TEST(IteratorPipelineTest, FromPromotesCustomIteratorLike) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 自定义 iterator-like：next 每次返回递增的值，3 次后 done
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "count", ctx->createNumber(0));
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* cnt = c->getProperty(self, "count");
            double n = numOf(cnt);
            Value* r = c->createObject();
            if (n >= 3) {
                c->setProperty(r, "value", c->createUndefined());
                c->setProperty(r, "done", c->createBoolean(true));
                return r;
            }
            c->setProperty(self, "count", c->createNumber(n + 1));
            c->setProperty(r, "value", c->createNumber(n * 10));
            c->setProperty(r, "done", c->createBoolean(false));
            return r;
        }, 0));

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* args[1] = {obj};
    Value* wrapped = ctx->call(from, iteratorFn, 1, args);
    ASSERT_EQ(wrapped->type()->kind(), TypeKind::IteratorWrapper);

    // 包装对象可被 Object.prototype.toString 识别为 Iterator
    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, wrapped, 0, nullptr)), "[object Iterator]");

    EXPECT_EQ(drain(ctx.get(), wrapped), "0|10|20|");
}

// ============ 场景 4：迭代器与对象模型协作 ============

TEST(IteratorPipelineTest, IteratorIntegratesWithObjectModel) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    static_cast<ArrayData*>(arr->data())->setElement(0, ctx->createNumber(1));
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);

    // Object.keys(iterator) → 空数组（next 在原型上）
    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* keysFn = ctx->getProperty(objectFn, "keys");
    Value* keyArgs[1] = {iter};
    Value* keys = ctx->call(keysFn, objectFn, 1, keyArgs);
    ASSERT_EQ(keys->type()->kind(), TypeKind::Array);
    EXPECT_EQ(static_cast<ArrayData*>(keys->data())->length(), 0u);

    // 迭代器是对象：可加自有属性、可设置原型
    ctx->setProperty(iter, "tag", ctx->createString("mine"));
    EXPECT_EQ(strOf(ctx->getProperty(iter, "tag")), "mine");

    // 迭代器参与字符串拼接（ToPrimitive → "[object Array Iterator]"）
    Value* concat = ctx->add(iter, ctx->createString("!"));
    ASSERT_EQ(concat->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(concat), "[object Array Iterator]!");

    // 迭代器不是函数 / 不是构造函数
    EXPECT_EQ(ctx->call(iter, ctx->createUndefined(), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->construct(iter, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

// ============ 场景 5：异常透传与收尾 ============

TEST(IteratorPipelineTest, ErrorsPropagateAndReturnCloses) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 自定义迭代器：第 2 次 next 抛异常
    Value* obj = ctx->createObject();
    ctx->setProperty(obj, "count", ctx->createNumber(0));
    ctx->setProperty(obj, "next", ctx->createNativeFunction(
        [](IContext* c, Value* self, size_t, Value*[]) {
            Value* cnt = c->getProperty(self, "count");
            double n = numOf(cnt);
            c->setProperty(self, "count", c->createNumber(n + 1));
            if (n >= 1) return c->createException(c->createString("boom"));
            Value* r = c->createObject();
            c->setProperty(r, "value", c->createNumber(1));
            c->setProperty(r, "done", c->createBoolean(false));
            return r;
        }, 0));

    Value* iteratorFn = globalOf(ctx.get(), "Iterator");
    Value* from = ctx->getProperty(iteratorFn, "from");
    Value* args[1] = {obj};
    Value* wrapped = ctx->call(from, iteratorFn, 1, args);
    EXPECT_EQ(callMethod(ctx.get(), wrapped, "next", 0, nullptr)->type()->kind(),
              TypeKind::Object);
    EXPECT_EQ(callMethod(ctx.get(), wrapped, "next", 0, nullptr)->type()->kind(),
              TypeKind::Exception);

    // 数组迭代器无 return 方法（收尾时 for-of 会跳过）
    Value* arr = ctx->createArray();
    Value* iter = callMethod(ctx.get(), arr, "values", 0, nullptr);
    EXPECT_EQ(ctx->getProperty(iter, "return")->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(closeIterator(ctx.get(), iter), nullptr);
}

// ============ 场景 6：大量迭代后 GC 正常回收 ============

TEST(IteratorPipelineTest, IteratorsUnderGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* kept = ctx->createArray();
    auto* kd = static_cast<ArrayData*>(kept->data());
    kd->setElement(0, ctx->createNumber(7));
    Value* keptIter = callMethod(ctx.get(), kept, "values", 0, nullptr);

    for (int i = 0; i < 300; ++i) {
        Value* arr = ctx->createArray();
        static_cast<ArrayData*>(arr->data())->setElement(0, ctx->createNumber(i));
        Value* it = callMethod(ctx.get(), arr, "values", 0, nullptr);
        callMethod(ctx.get(), it, "next", 0, nullptr);
    }
    ctx->gc()->collect();

    EXPECT_DOUBLE_EQ(numOf(ctx->getProperty(
                        callMethod(ctx.get(), keptIter, "next", 0, nullptr), "value")),
                     7.0);
    // Context 析构时 GC 回收全部 Value，ASan 验证无泄漏
}
