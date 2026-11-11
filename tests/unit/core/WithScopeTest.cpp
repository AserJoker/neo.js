// WithScopeTest.cpp - with 作用域（对象环境记录）单元测试
//
// 验证：
// - with 对象属性优先于外层绑定（读/写都走对象）
// - 对象没有该属性时回退外层作用域
// - 继承属性、原始值包装对象、访问器读写
// - 严格模式拒绝 with；null/undefined ToObject 失败
// - with 对象随作用域树保活，pop 后恢复 current

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Number.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::IContext;
using neo::core::NumberData;
using neo::core::Scope;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::detail::isErrorValue;
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

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }

// with 仅在非严格模式可用；测试默认关闭 global 的 strict 标注。
void makeSloppy(IContext* ctx) { ctx->global()->clearFlag("strict"); }

Value* g_accessorValue = nullptr;

Value* accessorGet(IContext* /*ctx*/, Value* /*self*/, size_t /*argc*/,
                   Value* /*argv*/[]) {
    return g_accessorValue;
}

Value* accessorSet(IContext* ctx, Value* /*self*/, size_t argc, Value* argv[]) {
    if (argc > 0) g_accessorValue = argv[0];
    return ctx->createUndefined();
}

}  // namespace

TEST(WithScopeTest, ObjectPropertyShadowsOuterBinding) {
    Fixture f;
    makeSloppy(f.ctx.get());
    f.ctx->define("x", f.ctx->createNumber(100));

    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "x", f.ctx->createNumber(1));

    Scope* ws = nullptr;
    Value* err = f.ctx->pushWithScope(obj, &ws);
    ASSERT_EQ(err, nullptr);
    ASSERT_NE(ws, nullptr);

    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("x")), 1.0);
    f.ctx->store("x", f.ctx->createNumber(2));
    EXPECT_EQ(numOf(f.ctx->getProperty(obj, f.ctx->createString("x"))), 2.0);

    f.ctx->popScope();
    // 外层绑定未被 with 写入影响。
    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("x")), 100.0);
}

TEST(WithScopeTest, MissingPropertyFallsBackToOuterBinding) {
    Fixture f;
    makeSloppy(f.ctx.get());
    f.ctx->define("y", f.ctx->createNumber(10));

    Value* obj = f.ctx->createObject();
    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(obj, &ws), nullptr);

    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("y")), 10.0);
    f.ctx->store("y", f.ctx->createNumber(11));
    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("y")), 11.0);

    f.ctx->popScope();
    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("y")), 11.0);
}

TEST(WithScopeTest, InheritedPropertyIsVisibleAndWriteCreatesOwn) {
    Fixture f;
    makeSloppy(f.ctx.get());

    Value* proto = f.ctx->createObject();
    f.ctx->defineProperty(proto, "p", f.ctx->createNumber(5));
    Value* obj = f.ctx->createObject();
    f.ctx->setPrototype(obj, proto);

    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(obj, &ws), nullptr);
    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("p")), 5.0);

    f.ctx->store("p", f.ctx->createNumber(6));
    EXPECT_EQ(numOf(f.ctx->getProperty(obj, f.ctx->createString("p"))), 6.0);
    EXPECT_EQ(numOf(f.ctx->getProperty(proto, f.ctx->createString("p"))), 5.0);
}

TEST(WithScopeTest, AccessorUsesGetAndSet) {
    Fixture f;
    makeSloppy(f.ctx.get());
    g_accessorValue = f.ctx->createNumber(1);

    Value* obj = f.ctx->createObject();
    Value* getter = f.ctx->createNativeFunction(accessorGet, 0);
    Value* setter = f.ctx->createNativeFunction(accessorSet, 1);
    f.ctx->defineAccessor(obj, f.ctx->createString("a"), getter, setter, true, true);

    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(obj, &ws), nullptr);
    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("a")), 1.0);

    f.ctx->store("a", f.ctx->createNumber(2));
    ASSERT_NE(g_accessorValue, nullptr);
    EXPECT_EQ(numOf(g_accessorValue), 2.0);
}

TEST(WithScopeTest, PrimitiveIsWrappedWithPrototype) {
    Fixture f;
    makeSloppy(f.ctx.get());

    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(f.ctx->createNumber(42), &ws), nullptr);
    // with (42) 的 toString 来自 Number.prototype。
    Value* fn = f.ctx->current()->lookupChain("toString");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->type()->kind(), TypeKind::Function);
}

TEST(WithScopeTest, NullAndUndefinedAreRejected) {
    Fixture f;
    makeSloppy(f.ctx.get());

    Scope* ws = nullptr;
    Value* err = f.ctx->pushWithScope(f.ctx->createNull(), &ws);
    ASSERT_NE(err, nullptr);
    EXPECT_TRUE(isErrorValue(err));

    err = f.ctx->pushWithScope(f.ctx->createUndefined(), &ws);
    ASSERT_NE(err, nullptr);
    EXPECT_TRUE(isErrorValue(err));
}

TEST(WithScopeTest, StrictModeRejectsWith) {
    Fixture f;
    // Context 默认 global 标注 strict。
    Scope* ws = nullptr;
    Value* err = f.ctx->pushWithScope(f.ctx->createObject(), &ws);
    ASSERT_NE(err, nullptr);
    EXPECT_TRUE(isErrorValue(err));
}

TEST(WithScopeTest, PopRestoresCurrent) {
    Fixture f;
    makeSloppy(f.ctx.get());

    Scope* before = f.ctx->current();
    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(f.ctx->createObject(), &ws), nullptr);
    EXPECT_NE(f.ctx->current(), before);
    f.ctx->popScope();
    EXPECT_EQ(f.ctx->current(), before);
}

TEST(WithScopeTest, WithObjectSurvivesGc) {
    Fixture f;
    makeSloppy(f.ctx.get());

    // 在临时作用域创建对象，pop 后它只由 WithScope 持有。
    f.ctx->pushNewScope(f.ctx->current());
    Value* obj = f.ctx->createObject();
    f.ctx->defineProperty(obj, "x", f.ctx->createNumber(1));
    f.ctx->popScope();

    Scope* ws = nullptr;
    ASSERT_EQ(f.ctx->pushWithScope(obj, &ws), nullptr);
    f.ctx->gc()->collect();

    EXPECT_EQ(numOf(f.ctx->current()->lookupChain("x")), 1.0);
}
