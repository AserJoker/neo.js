// ArrayTest.cpp - Array 类型与混合存储单元测试
//
// 验证（ADR-018 §6）：
// - ArrayType 单例、kind/name
// - 索引读写、length 语义（截断/扩展）、越界读 undefined
// - 空洞语义（delete / new Array(n) / 稀疏写入）
// - 混合存储：≤64 用 vector，>64 或 index≥64 迁移 map 且不回切
// - 非索引属性走 ObjectData（命名属性 / 原型链）
// - 索引键解析（前导零、负数、2^32-1 边界）
// - GC 遍历（索引元素 + 继承对象字段）
// - clone 引用语义
//
// 关联 ADR: 015（data 持子 value）、018（对象模型 §6）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IData.h"
#include "core/IGCEngine.h"
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
#include "TestHelpers.h"

namespace {

using neo::core::ArrayData;
using neo::core::IContext;
using neo::core::IGCVisitor;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::getArrayType;
using neo::core::kArrayDenseLimit;
using neo::core::parseArrayIndex;
using neo::infra::Console;
using neo::infra::IOutputSink;
using neo::infra::createMallocAllocator;

class NullSink : public IOutputSink {
public:
    void write(std::string_view) override {}
    void writeLine(std::string_view) override {}
    void flush() override {}
};

class CountingVisitor : public IGCVisitor {
public:
    int count = 0;
    void visit(Value* /*v*/) override { ++count; }
    void visitScope(neo::core::Scope* /*scope*/) override {}
};

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

ArrayData* dataOf(Value* v) { return static_cast<ArrayData*>(v->data()); }

}  // namespace

// ===== 类型基础 =====

TEST(ArrayTest, KindAndName) {
    EXPECT_EQ(getArrayType()->kind(), TypeKind::Array);
    EXPECT_STREQ(getArrayType()->name(), "array");
}

TEST(ArrayTest, SingletonIdentity) {
    EXPECT_EQ(getArrayType(), getArrayType());
}

// ===== 创建与原型 =====

TEST(ArrayTest, CreateEmptyArray) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ASSERT_NE(arr, ctx->oomValue());
    EXPECT_EQ(arr->type()->kind(), TypeKind::Array);
    EXPECT_EQ(dataOf(arr)->length(), 0u);
    EXPECT_EQ(ctx->getPrototype(arr), ctx->arrayPrototype());
}

TEST(ArrayTest, ArrayPrototypeIsArrayAndInheritsObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* proto = ctx->arrayPrototype();
    ASSERT_NE(proto, nullptr);
    EXPECT_EQ(proto->type()->kind(), TypeKind::Array);
    EXPECT_EQ(ctx->getPrototype(proto), ctx->objectPrototype());
}

TEST(ArrayTest, CreateArrayWithBindName) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray("xs");
    EXPECT_EQ(ctx->current()->lookup("xs"), arr);
}

// ===== 索引读写 =====

TEST(ArrayTest, SetAndGetIndex) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(10));
    ctx->setProperty(arr, "1", ctx->createNumber(20));

    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 10.0);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "1")), 20.0);
    EXPECT_EQ(dataOf(arr)->length(), 2u);
}

TEST(ArrayTest, NumberKeyIsSameAsStringKey) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, ctx->createNumber(0), ctx->createString("a"));
    EXPECT_EQ(strOf(ctx->getProperty(arr, "0")), "a");
}

TEST(ArrayTest, OutOfBoundsReadIsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    EXPECT_EQ(ctx->getProperty(arr, "5")->type()->kind(), TypeKind::Undefined);
}

TEST(ArrayTest, SparseWriteExtendsLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "3", ctx->createNumber(7));
    EXPECT_EQ(dataOf(arr)->length(), 4u);
    // 中间为空洞：读取 undefined，hasProperty false
    EXPECT_EQ(ctx->getProperty(arr, "1")->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "1"));
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "3"));
}

TEST(ArrayTest, HasPropertyForIndexAndLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "length"));
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "0"));
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "0"));
}

TEST(ArrayTest, DeleteIndexLeavesHole) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));

    EXPECT_EQ(ctx->deleteProperty(arr, "0")->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(dataOf(arr)->length(), 2u);  // length 不变
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "0"));
    EXPECT_EQ(ctx->getProperty(arr, "0")->type()->kind(), TypeKind::Undefined);
}

TEST(ArrayTest, DeleteLengthReturnsException) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->deleteProperty(arr, "length")->type()->kind(), TypeKind::Exception);
}

// ===== length 语义 =====

TEST(ArrayTest, ReadLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(numOf(ctx->getProperty(arr, "length")), 0.0);
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    EXPECT_EQ(numOf(ctx->getProperty(arr, "length")), 2.0);
}

TEST(ArrayTest, WriteLengthTruncates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (int i = 0; i < 5; ++i) {
        ctx->setProperty(arr, std::to_string(i), ctx->createNumber(i));
    }
    ctx->setProperty(arr, "length", ctx->createNumber(2));
    EXPECT_EQ(dataOf(arr)->length(), 2u);
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "1"));
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "2"));
}

TEST(ArrayTest, WriteLengthExtendsWithHoles) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "length", ctx->createNumber(3));
    EXPECT_EQ(dataOf(arr)->length(), 3u);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "0"));
    EXPECT_EQ(ctx->getProperty(arr, "2")->type()->kind(), TypeKind::Undefined);
}

TEST(ArrayTest, InvalidLengthRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->setProperty(arr, "length", ctx->createNumber(-1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(arr, "length", ctx->createNumber(1.5))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(arr, "length", ctx->createString("2"))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(dataOf(arr)->length(), 0u);
}

// ===== 混合存储 =====

TEST(ArrayTest, SmallArrayUsesDenseStorage) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    for (uint32_t i = 0; i < kArrayDenseLimit; ++i) {
        ctx->setProperty(arr, ctx->createNumber(static_cast<double>(i)),
                         ctx->createNumber(static_cast<double>(i)));
    }
    EXPECT_FALSE(dataOf(arr)->sparseMode);
    EXPECT_EQ(dataOf(arr)->dense.size(), kArrayDenseLimit);
}

TEST(ArrayTest, LargeIndexMigratesToSparse) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "100", ctx->createNumber(2));

    EXPECT_TRUE(dataOf(arr)->sparseMode);
    EXPECT_EQ(dataOf(arr)->length(), 101u);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "100")), 2.0);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "50"));
}

TEST(ArrayTest, SparseModeDoesNotRevert) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "100", ctx->createNumber(1));
    ASSERT_TRUE(dataOf(arr)->sparseMode);
    ctx->setProperty(arr, "length", ctx->createNumber(0));
    EXPECT_TRUE(dataOf(arr)->sparseMode);  // 不回切
    EXPECT_EQ(dataOf(arr)->length(), 0u);
}

TEST(ArrayTest, LargeLengthMigratesToSparse) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "length", ctx->createNumber(1000));
    EXPECT_TRUE(dataOf(arr)->sparseMode);
    EXPECT_EQ(dataOf(arr)->length(), 1000u);
}

TEST(ArrayTest, SparseTruncateDropsElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "100", ctx->createNumber(1));
    ctx->setProperty(arr, "200", ctx->createNumber(2));
    ctx->setProperty(arr, "length", ctx->createNumber(150));
    EXPECT_EQ(dataOf(arr)->length(), 150u);
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "100"));
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "200"));
}

// ===== 索引键解析 =====

TEST(ArrayTest, ParseArrayIndexAcceptsCanonicalForms) {
    uint32_t out = 99;
    EXPECT_TRUE(parseArrayIndex("0", out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(parseArrayIndex("123", out));
    EXPECT_EQ(out, 123u);
    EXPECT_TRUE(parseArrayIndex("4294967294", out));
    EXPECT_EQ(out, 4294967294u);
}

TEST(ArrayTest, ParseArrayIndexRejectsNonCanonical) {
    uint32_t out = 0;
    EXPECT_FALSE(parseArrayIndex("", out));
    EXPECT_FALSE(parseArrayIndex("01", out));
    EXPECT_FALSE(parseArrayIndex("-1", out));
    EXPECT_FALSE(parseArrayIndex("1.5", out));
    EXPECT_FALSE(parseArrayIndex("abc", out));
    EXPECT_FALSE(parseArrayIndex("4294967295", out));  // 2^32-1 不是索引
    EXPECT_FALSE(parseArrayIndex("99999999999", out));
}

TEST(ArrayTest, LeadingZeroKeyGoesToNamedProperties) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "01", ctx->createNumber(5));
    EXPECT_EQ(dataOf(arr)->length(), 0u);  // 不扩展 length
    EXPECT_EQ(numOf(ctx->getProperty(arr, "01")), 5.0);
}

// ===== 命名属性与原型链 =====

TEST(ArrayTest, NamedPropertyStoredInObjectPart) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "foo", ctx->createString("bar"));
    EXPECT_EQ(strOf(ctx->getProperty(arr, "foo")), "bar");
    EXPECT_EQ(dataOf(arr)->length(), 0u);
    EXPECT_EQ(dataOf(arr)->properties.count("foo"), 1u);
}

TEST(ArrayTest, InheritsFromArrayPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* pushFn = ctx->getProperty(arr, "push");
    EXPECT_EQ(pushFn->type()->kind(), TypeKind::Function);
}

TEST(ArrayTest, SymbolPropertyWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* sym = ctx->createSymbol("k");
    ctx->setProperty(arr, sym, ctx->createNumber(9));
    EXPECT_EQ(numOf(ctx->getProperty(arr, sym)), 9.0);
    EXPECT_EQ(dataOf(arr)->length(), 0u);
}

TEST(ArrayTest, SymbolKeyGoesThroughObjectPartForAllOps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* sym = ctx->createSymbol("k");

    // has：未定义 → false（走 ObjectType）
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, sym));
    // defineProperty 走 ObjectType
    ctx->defineProperty(arr, sym, ctx->createNumber(1), true, false, true);
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, sym));
    EXPECT_EQ(numOf(ctx->getProperty(arr, sym)), 1.0);
    // defineAccessor 走 ObjectType
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(7); }, 0);
    EXPECT_EQ(ctx->defineAccessor(arr, sym, getter, nullptr), arr);
    EXPECT_EQ(numOf(ctx->getProperty(arr, sym)), 7.0);
    // delete 走 ObjectType
    EXPECT_EQ(ctx->deleteProperty(arr, sym)->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, sym));
}

TEST(ArrayTest, DeleteNamedPropertyGoesThroughObjectPart) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "foo", ctx->createNumber(1));
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "foo"));
    EXPECT_EQ(ctx->deleteProperty(arr, "foo")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "foo"));
}

TEST(ArrayTest, DeleteMissingIndexReturnsTrue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    // 稠密模式下的空洞删除
    EXPECT_EQ(ctx->deleteProperty(arr, "3")->type()->kind(), TypeKind::Boolean);
    // 稀疏模式下的不存在索引删除
    ctx->setProperty(arr, "100", ctx->createNumber(1));
    EXPECT_EQ(ctx->deleteProperty(arr, "50")->type()->kind(), TypeKind::Boolean);
    EXPECT_EQ(ctx->deleteProperty(arr, "100")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "100"));
}

TEST(ArrayTest, ToSparseIsIdempotent) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    auto* d = dataOf(arr);
    d->toSparse();
    ASSERT_TRUE(d->sparseMode);
    d->toSparse();  // 已稀疏：直接返回
    EXPECT_TRUE(d->sparseMode);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 1.0);
}

TEST(ArrayTest, DenseToSparseMigrationKeepsExistingElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    // 触发迁移
    ctx->setProperty(arr, "200", ctx->createNumber(4));
    ASSERT_TRUE(dataOf(arr)->sparseMode);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 1.0);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "1")), 2.0);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "2")), 3.0);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "200")), 4.0);
}

// ===== defineProperty =====

TEST(ArrayTest, DefineIndexProperty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->defineProperty(arr, "2", ctx->createNumber(3));
    EXPECT_EQ(numOf(ctx->getProperty(arr, "2")), 3.0);
    EXPECT_EQ(dataOf(arr)->length(), 3u);
}

TEST(ArrayTest, DefineIndexWithNonDefaultAttributesRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->defineProperty(arr, "0", ctx->createNumber(1), false, true, true)
                  ->type()->kind(),
              TypeKind::Exception);
}

TEST(ArrayTest, DefineLengthRejectsEnumerableOrConfigurable) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->defineProperty(arr, "length", ctx->createNumber(1), true, true, false)
                  ->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(arr, "length", ctx->createNumber(1), true, false, true)
                  ->type()->kind(),
              TypeKind::Exception);
}

TEST(ArrayTest, DefineAccessorOnIndexRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    EXPECT_EQ(ctx->defineAccessor(arr, "0", getter, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->defineAccessor(arr, "length", getter, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(ArrayTest, DefineAccessorOnNamedPropertyWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(42); }, 0);
    EXPECT_EQ(ctx->defineAccessor(arr, "x", getter, nullptr), arr);
    EXPECT_EQ(numOf(ctx->getProperty(arr, "x")), 42.0);
}

// ===== 运算 =====

TEST(ArrayTest, ToStringJoinsWithComma) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createString("two"));
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    EXPECT_EQ(strOf(ctx->toString(arr)), "1,two,3");
}

TEST(ArrayTest, ToStringSkipsHolesAndNullish) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNull());
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    ctx->setProperty(arr, "4", ctx->createNumber(5));  // 3 是空洞
    EXPECT_EQ(strOf(ctx->toString(arr)), "1,,3,,5");
}

TEST(ArrayTest, PlusWithNumberConcatenates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));
    EXPECT_EQ(strOf(ctx->add(arr, ctx->createNumber(3))), "1,23");
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(3), arr)), "31,2");
}

TEST(ArrayTest, ArithmeticConvertsViaString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(10));
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(arr, ctx->createNumber(4))), 6.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(arr, ctx->createNumber(2))), 20.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(arr, ctx->createNumber(5))), 2.0);
}

TEST(ArrayTest, EmptyArrayToNumberIsZero) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    // [] → "" → 0；[] - 1 === -1
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(arr, ctx->createNumber(1))), -1.0);
}

TEST(ArrayTest, CallAndConstructAreRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->call(arr, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(arr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

// ===== GC 与 clone =====

TEST(ArrayTest, EnumerateChildrenVisitsElements) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "1", ctx->createNumber(2));

    CountingVisitor visitor;
    dataOf(arr)->enumerateChildren(&visitor);
    // 2 个索引元素 + prototype（Array.prototype）
    EXPECT_EQ(visitor.count, 3);
}

TEST(ArrayTest, EnumerateChildrenSkipsHoles) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "5", ctx->createNumber(2));

    CountingVisitor visitor;
    dataOf(arr)->enumerateChildren(&visitor);
    // 2 个元素 + prototype
    EXPECT_EQ(visitor.count, 3);
}

TEST(ArrayTest, IndexChainStopsAtNonObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 原型链上放一个普通对象（无 elements），索引查找应跳过该层到 null 终止
    Value* arr = ctx->createArray();
    Value* mid = ctx->createObject();
    ctx->setPrototype(arr, mid);
    EXPECT_EQ(ctx->getProperty(arr, "0")->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(neo::test::hasProperty(ctx, arr, "0"));
}

TEST(ArrayTest, IndexFoundOnObjectLayerOfChain) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 原型链：arr → 另一个数组（有元素 0）
    Value* parent = ctx->createArray();
    ctx->setProperty(parent, "0", ctx->createString("fromParent"));
    Value* arr = ctx->createArray();
    ctx->setPrototype(arr, parent);
    EXPECT_EQ(strOf(ctx->getProperty(arr, "0")), "fromParent");
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "0"));
}

TEST(ArrayTest, EnumerateChildrenSparseMode) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "100", ctx->createNumber(1));
    ctx->setProperty(arr, "200", ctx->createNumber(2));

    CountingVisitor visitor;
    dataOf(arr)->enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 3);  // 2 元素 + prototype
}

TEST(ArrayTest, CloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));

    auto* d = dataOf(arr);
    int before = d->ref();
    auto* cloned = static_cast<ArrayData*>(d->clone(alloc.get()));
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), before + 1);
    d->refDec();
}

TEST(ArrayTest, GetOnHoleFallsThroughToPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // 在 Array.prototype 上放一个索引属性，空洞读取应沿原型链命中
    Value* proto = ctx->arrayPrototype();
    ctx->defineProperty(proto, "1", ctx->createString("inherited"), true, true, true);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    ctx->setProperty(arr, "2", ctx->createNumber(3));
    EXPECT_EQ(strOf(ctx->getProperty(arr, "1")), "inherited");
    // `in` 语义含原型链：原型上有该索引 → true
    EXPECT_TRUE(neo::test::hasProperty(ctx, arr, "1"));
}

TEST(ArrayTest, ElementsAliveThroughArrayDuringGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* elem = ctx->createNumber(42);  // 仅被数组持有
    ctx->setProperty(arr, "0", elem);

    ctx->gc()->collect();
    EXPECT_EQ(numOf(ctx->getProperty(arr, "0")), 42.0);
}

// ===== 原型操作 =====

TEST(ArrayTest, SetPrototypeWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(arr, proto), arr);
    EXPECT_EQ(ctx->getPrototype(arr), proto);
}

TEST(ArrayTest, SetPrototypeRejectsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    EXPECT_EQ(ctx->setPrototype(arr, ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
}
