// ClassDataTest.cpp - ClassData / FunctionCreateOptions / SourceSlice（ADR-047 P1-2）
//
// 验证：
// - class 构造器使用 ClassData，携带 isDerived/superConstructor
// - hasPrototypeProperty=false 不创建 prototype 属性；constructible 可关闭
// - SourceSlice 引用计数与 Function.prototype.toString
// - homeObject / superConstructor / fields 由 GC 遍历保活

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/Scope.h"
#include "core/SourceText.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Function.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <memory>

namespace {

using neo::core::ClassData;
using neo::core::FunctionCreateOptions;
using neo::core::FunctionData;
using neo::core::IContext;
using neo::core::NumberData;
using neo::core::SourceSlice;
using neo::core::SourceText;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::createSourceText;
using neo::core::releaseSourceText;
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

Value* noop(IContext* ctx, Value*, size_t, Value*[]) { return ctx->createUndefined(); }

double numOf(Value* v) { return static_cast<NumberData*>(v->data())->value; }
const std::string& strOf(Value* v) { return static_cast<StringData*>(v->data())->value; }

Value* toStringOf(IContext* ctx, Value* fn) {
    Value* toString = ctx->getProperty(fn, ctx->createString("toString"));
    return ctx->call(toString, fn, 0, nullptr);
}

// 只查自有属性键（hasProperty 会沿原型链，Function.prototype 自身也有 prototype）。
bool hasOwnKey(IContext* ctx, Value* obj, std::string_view name) {
    Value* keys = ctx->ownPropertyKeys(obj, false, false);
    auto* arr = static_cast<neo::core::ArrayData*>(keys->data());
    for (uint32_t i = 0; i < arr->length(); ++i) {
        Value* k = arr->elementAt(i);
        if (k != nullptr && k->type()->kind() == TypeKind::String &&
            static_cast<StringData*>(k->data())->value == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(ClassDataTest, ClassConstructorUsesClassData) {
    Fixture f;
    Value* superCtor = f.ctx->createNativeFunction(noop, 0);

    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.isDerived = true;
    opts.hasPrototypeProperty = false;
    opts.superConstructor = superCtor;

    Value* cls = f.ctx->createNativeFunction(noop, 0, {}, opts);
    ASSERT_EQ(cls->type()->kind(), TypeKind::Function);
    auto* cd = static_cast<ClassData*>(cls->data());
    EXPECT_TRUE(cd->isClassConstructor);
    EXPECT_TRUE(cd->isDerived);
    EXPECT_EQ(cd->superConstructor, superCtor);
    EXPECT_TRUE(cd->constructible);
    // class 构造器由 VM 显式定义 non-writable prototype，创建时不应自动生成。
    EXPECT_FALSE(hasOwnKey(f.ctx.get(), cls, "prototype"));
}

TEST(ClassDataTest, MethodHasNoPrototypeAndIsNotConstructor) {
    Fixture f;
    FunctionCreateOptions opts;
    opts.constructible = false;
    opts.hasPrototypeProperty = false;

    Value* method = f.ctx->createNativeFunction(noop, 0, {}, opts);
    EXPECT_FALSE(hasOwnKey(f.ctx.get(), method, "prototype"));
    EXPECT_FALSE(neo::core::detail::isConstructor(method));
}

TEST(ClassDataTest, SourceSliceDrivesFunctionToString) {
    Fixture f;
    SourceText* source = createSourceText("class C { m() {} }");
    SourceSlice slice{source, 0, static_cast<uint32_t>(source->text.size())};

    FunctionCreateOptions opts;
    opts.source = slice;
    Value* fn = f.ctx->createNativeFunction(noop, 0, {}, opts);
    EXPECT_EQ(source->refCount, 2u);  // 创建者 + 函数

    releaseSourceText(source);  // 创建者释放，函数仍持有
    EXPECT_EQ(source->refCount, 1u);

    Value* text = toStringOf(f.ctx.get(), fn);
    ASSERT_EQ(text->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(text), "class C { m() {} }");
}

TEST(ClassDataTest, SetFunctionSourceReplacesAndReleases) {
    Fixture f;
    Value* fn = f.ctx->createNativeFunction(noop, 0);

    SourceText* a = createSourceText("aaaa");
    SourceText* b = createSourceText("bbbb");
    f.ctx->setFunctionSource(fn, SourceSlice{a, 0, 4});
    EXPECT_EQ(a->refCount, 2u);

    f.ctx->setFunctionSource(fn, SourceSlice{b, 0, 4});
    EXPECT_EQ(a->refCount, 1u);  // 函数已释放 a
    EXPECT_EQ(b->refCount, 2u);

    releaseSourceText(a);
    releaseSourceText(b);
    EXPECT_EQ(b->refCount, 1u);

    Value* text = toStringOf(f.ctx.get(), fn);
    ASSERT_EQ(text->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(text), "bbbb");
}

TEST(ClassDataTest, SliceIsClampedToSourceBounds) {
    Fixture f;
    SourceText* source = createSourceText("abc");
    FunctionCreateOptions opts;
    opts.source = SourceSlice{source, 1, 100};  // 越界 length
    Value* fn = f.ctx->createNativeFunction(noop, 0, {}, opts);
    releaseSourceText(source);

    Value* text = toStringOf(f.ctx.get(), fn);
    ASSERT_EQ(text->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(text), "bc");
}

TEST(ClassDataTest, HomeObjectSurvivesGc) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Value* home = f.ctx->createObject();
    f.ctx->defineProperty(home, "x", f.ctx->createNumber(5));
    f.ctx->popScope();

    FunctionCreateOptions opts;
    opts.homeObject = home;
    Value* fn = f.ctx->createNativeFunction(noop, 0, {}, opts);
    f.ctx->gc()->collect();

    auto* d = static_cast<FunctionData*>(fn->data());
    EXPECT_EQ(d->homeObject, home);
    Value* x = f.ctx->getProperty(home, f.ctx->createString("x"));
    ASSERT_EQ(x->type()->kind(), TypeKind::Number);
    EXPECT_EQ(numOf(x), 5.0);
}

TEST(ClassDataTest, SuperConstructorAndFieldsSurviveGc) {
    Fixture f;
    f.ctx->pushNewScope(f.ctx->current());
    Value* superCtor = f.ctx->createNativeFunction(noop, 0);
    Value* key = f.ctx->createString("field");
    Value* initializer = f.ctx->createNativeFunction(noop, 0);
    f.ctx->popScope();

    FunctionCreateOptions opts;
    opts.isClassConstructor = true;
    opts.isDerived = true;
    opts.superConstructor = superCtor;
    Value* cls = f.ctx->createNativeFunction(noop, 0, {}, opts);

    auto* cd = static_cast<ClassData*>(cls->data());
    neo::core::InstanceElement field;
    field.key = key;
    field.initializer = initializer;
    cd->instanceElements.push_back(field);

    f.ctx->gc()->collect();

    EXPECT_EQ(cd->superConstructor, superCtor);
    ASSERT_EQ(cd->instanceElements.size(), 1u);
    EXPECT_EQ(cd->instanceElements[0].key, key);
    EXPECT_EQ(cd->instanceElements[0].initializer, initializer);
}
