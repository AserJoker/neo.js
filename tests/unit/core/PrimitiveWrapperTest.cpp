// PrimitiveWrapperTest.cpp - 原始值包装对象单元测试
//
// 验证（ES2024 §10.4 / §20.3 / §21.1 / §22.1 / §20.4）：
// - PrimitiveWrapperType 单例；createPrimitiveWrapper 按 primitive 类型挂对应原型
// - Boolean/Number/String 构造函数：call 返回原始值，new 返回包装对象
// - *Prototype.valueOf / toString；this 可为原始值或包装对象
// - Object.prototype.toString 的 tag 按 [[PrimitiveValue]] 类型
// - Object(value) 对原始值返回包装对象，对对象原样返回
// - String 包装的 length 与索引属性（只读）
// - 运算：ToPrimitive 拆 [[PrimitiveValue]] 后按原始值规则
// - GC 遍历与 clone 引用语义
//
// 关联 ADR: 020（标准库组织）、018（对象模型）、019（赋值语义 §2）

#include "core/IContext.h"
#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Number.h"
#include "core/types/Object.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/String.h"
#include "infra/Console.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>
#include "TestHelpers.h"

#include <cmath>
#include <string>

namespace {

using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::IGCVisitor;
using neo::core::NumberData;
using neo::core::PrimitiveWrapperData;
using neo::core::StringData;
using neo::core::TypeKind;
using neo::core::Value;
using neo::core::createContext;
using neo::core::getPrimitiveWrapperType;
using neo::core::wrapperPrimitiveValue;
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
bool boolOf(Value* v) { return static_cast<BooleanData*>(v->data())->value; }
PrimitiveWrapperData* dataOf(Value* v) {
    return static_cast<PrimitiveWrapperData*>(v->data());
}

Value* globalOf(IContext* ctx, const char* name) { return ctx->global()->lookup(name); }

Value* callMethod(IContext* ctx, Value* self, const char* name, size_t argc, Value* argv[]) {
    return ctx->call(ctx->getProperty(self, name), self, argc, argv);
}

}  // namespace

// ===== 类型与创建 =====

TEST(PrimitiveWrapperTest, KindAndName) {
    EXPECT_EQ(getPrimitiveWrapperType()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_STREQ(getPrimitiveWrapperType()->name(), "object");
    EXPECT_EQ(getPrimitiveWrapperType(), getPrimitiveWrapperType());
}

TEST(PrimitiveWrapperTest, CreateWrapsEachPrimitiveKind) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    struct Case {
        Value* primitive;
        Value* expectedProto;
    };
    Value* cases[4][2] = {
        {ctx->createBoolean(true), ctx->booleanPrototype()},
        {ctx->createNumber(1.5), ctx->numberPrototype()},
        {ctx->createString("s"), ctx->stringPrototype()},
        {ctx->createSymbol("sym"), ctx->symbolPrototype()},
    };
    for (auto& c : cases) {
        Value* w = ctx->createPrimitiveWrapper(c[0]);
        ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
        EXPECT_EQ(wrapperPrimitiveValue(w), c[0]);
        EXPECT_EQ(ctx->getPrototype(w), c[1]);
    }
}

TEST(PrimitiveWrapperTest, CreateRejectsNonPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(ctx->createPrimitiveWrapper(ctx->createObject())->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->createPrimitiveWrapper(ctx->createUndefined())->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->createPrimitiveWrapper(ctx->createArray())->type()->kind(),
              TypeKind::Exception);
}

TEST(PrimitiveWrapperTest, PrototypesInheritObjectPrototype) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    for (Value* proto : {ctx->booleanPrototype(), ctx->numberPrototype(),
                         ctx->stringPrototype()}) {
        ASSERT_NE(proto, nullptr);
        EXPECT_EQ(proto->type()->kind(), TypeKind::PrimitiveWrapper);
        EXPECT_EQ(ctx->getPrototype(proto), ctx->objectPrototype());
    }
    // Symbol.prototype 是普通对象（规范如此）
    EXPECT_EQ(ctx->symbolPrototype()->type()->kind(), TypeKind::Object);
    EXPECT_EQ(ctx->getPrototype(ctx->symbolPrototype()), ctx->objectPrototype());
}

TEST(PrimitiveWrapperTest, PrototypesHoldDefaultPrimitiveValues) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_FALSE(boolOf(wrapperPrimitiveValue(ctx->booleanPrototype())));
    EXPECT_EQ(numOf(wrapperPrimitiveValue(ctx->numberPrototype())), 0.0);
    EXPECT_EQ(strOf(wrapperPrimitiveValue(ctx->stringPrototype())), "");
}

// ===== 构造函数 =====

TEST(PrimitiveWrapperTest, BooleanCallReturnsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* booleanFn = globalOf(ctx.get(), "Boolean");
    ASSERT_NE(booleanFn, nullptr);
    EXPECT_EQ(booleanFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(ctx->getPrototype(booleanFn), ctx->functionPrototype());

    Value* args[1] = {ctx->createNumber(1)};
    Value* r = ctx->call(booleanFn, nullptr, 1, args);
    EXPECT_EQ(r->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(r));
}

TEST(PrimitiveWrapperTest, BooleanNewReturnsWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* booleanFn = globalOf(ctx.get(), "Boolean");
    Value* args[1] = {ctx->createString("")};
    Value* w = ctx->construct(booleanFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_FALSE(boolOf(wrapperPrimitiveValue(w)));
    EXPECT_EQ(ctx->getPrototype(w), ctx->booleanPrototype());
}

TEST(PrimitiveWrapperTest, NumberCallReturnsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createString("42")};
    Value* r = ctx->call(numberFn, nullptr, 1, args);
    EXPECT_EQ(r->type()->kind(), TypeKind::Number);
    EXPECT_DOUBLE_EQ(numOf(r), 42.0);
}

TEST(PrimitiveWrapperTest, NumberNewReturnsWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createString("42")};
    Value* w = ctx->construct(numberFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_DOUBLE_EQ(numOf(wrapperPrimitiveValue(w)), 42.0);
    EXPECT_EQ(ctx->getPrototype(w), ctx->numberPrototype());
}

TEST(PrimitiveWrapperTest, BooleanCallCoversToBooleanBranches) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* booleanFn = globalOf(ctx.get(), "Boolean");
    struct Case {
        Value* v;
        bool expected;
    };
    Value* cases[9][2] = {
        {ctx->createUndefined(), nullptr},
        {ctx->createNull(), nullptr},
        {ctx->createBoolean(false), nullptr},
        {ctx->createNumber(0), nullptr},
        {ctx->createNumber(std::nan("")), nullptr},
        {ctx->createNumber(-1), nullptr},
        {ctx->createString(""), nullptr},
        {ctx->createString("x"), nullptr},
        {ctx->createObject(), nullptr},
    };
    const bool expected[9] = {false, false, false, false, false, true,
                              false, true,  true};
    for (int i = 0; i < 9; ++i) {
        Value* args[1] = {cases[i][0]};
        Value* r = ctx->call(booleanFn, nullptr, 1, args);
        ASSERT_EQ(r->type()->kind(), TypeKind::Boolean) << i;
        EXPECT_EQ(boolOf(r), expected[i]) << i;
    }
}

TEST(PrimitiveWrapperTest, NumberCallCoversToNumberBranches) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    struct Case {
        Value* v;
        double expected;
    };
    Value* cases[7][2] = {
        {ctx->createUndefined(), nullptr},
        {ctx->createNull(), nullptr},
        {ctx->createBoolean(true), nullptr},
        {ctx->createBoolean(false), nullptr},
        {ctx->createString("3.5"), nullptr},
        {ctx->createArray(), nullptr},
        {ctx->createPrimitiveWrapper(ctx->createNumber(7)), nullptr},
    };
    const double expected[7] = {std::nan(""), 0.0, 1.0, 0.0, 3.5, 0.0, 7.0};
    for (int i = 0; i < 7; ++i) {
        Value* args[1] = {cases[i][0]};
        double got = numOf(ctx->call(numberFn, nullptr, 1, args));
        if (std::isnan(expected[i])) {
            EXPECT_TRUE(std::isnan(got)) << i;
        } else {
            EXPECT_DOUBLE_EQ(got, expected[i]) << i;
        }
    }
}

TEST(PrimitiveWrapperTest, NumberCallWithNoArgsIsZero) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    EXPECT_DOUBLE_EQ(numOf(ctx->call(numberFn, nullptr, 0, nullptr)), 0.0);
}

TEST(PrimitiveWrapperTest, NumberRejectsSymbol) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createSymbol("s")};
    EXPECT_EQ(ctx->call(numberFn, nullptr, 1, args)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(numberFn, 1, args)->type()->kind(), TypeKind::Exception);
}

TEST(PrimitiveWrapperTest, StringCallReturnsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    Value* args[1] = {ctx->createNumber(12)};
    Value* r = ctx->call(stringFn, nullptr, 1, args);
    EXPECT_EQ(r->type()->kind(), TypeKind::String);
    EXPECT_EQ(strOf(r), "12");
}

TEST(PrimitiveWrapperTest, StringCallOnSymbolUsesSymbolToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    Value* args[1] = {ctx->createSymbol("desc")};
    EXPECT_EQ(strOf(ctx->call(stringFn, nullptr, 1, args)), "Symbol(desc)");
}

TEST(PrimitiveWrapperTest, StringNewReturnsWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    Value* args[1] = {ctx->createNumber(7)};
    Value* w = ctx->construct(stringFn, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(strOf(wrapperPrimitiveValue(w)), "7");
    EXPECT_EQ(ctx->getPrototype(w), ctx->stringPrototype());
}

TEST(PrimitiveWrapperTest, StringCallWithNoArgsIsEmpty) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    EXPECT_EQ(strOf(ctx->call(stringFn, nullptr, 0, nullptr)), "");
}

// ===== prototype 方法 =====

TEST(PrimitiveWrapperTest, BooleanPrototypeValueOfAndToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* booleanFn = globalOf(ctx.get(), "Boolean");
    Value* args[1] = {ctx->createBoolean(true)};
    Value* w = ctx->construct(booleanFn, 1, args);

    Value* v = callMethod(ctx.get(), w, "valueOf", 0, nullptr);
    EXPECT_EQ(v->type()->kind(), TypeKind::Boolean);
    EXPECT_TRUE(boolOf(v));

    EXPECT_EQ(strOf(callMethod(ctx.get(), w, "toString", 0, nullptr)), "true");

    // 原始值作为 this 也可（ES2024 §20.3.3.2 允许）：直接取 prototype 方法调用。
    // 注：原始值的属性访问（自动 ToObject）本阶段尚未实现，故不经 getProperty。
    Value* booleanProto = ctx->booleanPrototype();
    Value* toStringFn = ctx->getProperty(booleanProto, "toString");
    EXPECT_EQ(strOf(ctx->call(toStringFn, ctx->createBoolean(false), 0, nullptr)), "false");
}

TEST(PrimitiveWrapperTest, NumberPrototypeValueOfAndToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberFn = globalOf(ctx.get(), "Number");
    Value* args[1] = {ctx->createNumber(3.5)};
    Value* w = ctx->construct(numberFn, 1, args);

    EXPECT_DOUBLE_EQ(numOf(callMethod(ctx.get(), w, "valueOf", 0, nullptr)), 3.5);
    EXPECT_EQ(strOf(callMethod(ctx.get(), w, "toString", 0, nullptr)), "3.5");
}

TEST(PrimitiveWrapperTest, StringPrototypeValueOfAndToString) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* stringFn = globalOf(ctx.get(), "String");
    Value* args[1] = {ctx->createString("hi")};
    Value* w = ctx->construct(stringFn, 1, args);

    EXPECT_EQ(strOf(callMethod(ctx.get(), w, "valueOf", 0, nullptr)), "hi");
    EXPECT_EQ(strOf(callMethod(ctx.get(), w, "toString", 0, nullptr)), "hi");
}

TEST(PrimitiveWrapperTest, SymbolPrototypeValueOf) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* sym = ctx->createSymbol("x");
    Value* w = ctx->createPrimitiveWrapper(sym);
    EXPECT_EQ(callMethod(ctx.get(), w, "valueOf", 0, nullptr), sym);
    EXPECT_EQ(strOf(callMethod(ctx.get(), w, "toString", 0, nullptr)), "Symbol(x)");
}

TEST(PrimitiveWrapperTest, ValueOfRejectsWrongThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* booleanFn = globalOf(ctx.get(), "Boolean");
    Value* booleanProto = ctx->getProperty(booleanFn, "prototype");
    Value* valueOf = ctx->getProperty(booleanProto, "valueOf");
    EXPECT_EQ(ctx->call(valueOf, ctx->createNumber(1), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->call(valueOf, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(PrimitiveWrapperTest, ToStringRejectsWrongThis) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* numberProto = ctx->numberPrototype();
    Value* toStringFn = ctx->getProperty(numberProto, "toString");
    EXPECT_EQ(ctx->call(toStringFn, ctx->createString("x"), 0, nullptr)->type()->kind(),
              TypeKind::Exception);
}

TEST(PrimitiveWrapperTest, ConstructorAndPrototypeCrossReference) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    struct Case {
        const char* name;
        Value* proto;
    };
    Value* cases[3][2] = {
        {nullptr, ctx->booleanPrototype()},
        {nullptr, ctx->numberPrototype()},
        {nullptr, ctx->stringPrototype()},
    };
    const char* names[3] = {"Boolean", "Number", "String"};
    for (int i = 0; i < 3; ++i) {
        Value* fn = globalOf(ctx.get(), names[i]);
        EXPECT_EQ(ctx->getProperty(fn, "prototype"), cases[i][1]) << names[i];
        EXPECT_EQ(ctx->getProperty(cases[i][1], "constructor"), fn) << names[i];
    }
}

TEST(PrimitiveWrapperTest, ToStringTagOnPrototypes) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* tag = ctx->getWellKnownSymbol(neo::core::WellKnownSymbol::ToStringTag);
    EXPECT_EQ(strOf(ctx->getProperty(ctx->booleanPrototype(), tag)), "Boolean");
    EXPECT_EQ(strOf(ctx->getProperty(ctx->numberPrototype(), tag)), "Number");
    EXPECT_EQ(strOf(ctx->getProperty(ctx->stringPrototype(), tag)), "String");
    EXPECT_EQ(strOf(ctx->getProperty(ctx->symbolPrototype(), tag)), "Symbol");
}

// ===== Object.prototype.toString 与 Object(value) =====

TEST(PrimitiveWrapperTest, ObjectProtoToStringTagsWrapperByPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* toStringFn = ctx->getProperty(ctx->objectPrototype(), "toString");
    struct Case {
        Value* primitive;
        const char* tag;
    };
    Value* cases[4][2] = {
        {ctx->createBoolean(true), nullptr},
        {ctx->createNumber(1), nullptr},
        {ctx->createString("s"), nullptr},
        {ctx->createSymbol("s"), nullptr},
    };
    const char* tags[4] = {"[object Boolean]", "[object Number]", "[object String]",
                           "[object Symbol]"};
    for (int i = 0; i < 4; ++i) {
        Value* w = ctx->createPrimitiveWrapper(cases[i][0]);
        EXPECT_EQ(strOf(ctx->call(toStringFn, w, 0, nullptr)), tags[i]);
    }
}

TEST(PrimitiveWrapperTest, ObjectCtorWrapsPrimitives) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* args[1] = {ctx->createString("s")};
    Value* w = ctx->call(objectFn, nullptr, 1, args);
    ASSERT_EQ(w->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(ctx->getPrototype(w), ctx->stringPrototype());
}

TEST(PrimitiveWrapperTest, ObjectCtorReturnsWrapperAsIs) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* objectFn = globalOf(ctx.get(), "Object");
    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    Value* args[1] = {w};
    EXPECT_EQ(ctx->call(objectFn, nullptr, 1, args), w);
}

TEST(PrimitiveWrapperTest, HasOwnPropertyWorksOnWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* hasOwn = ctx->getProperty(ctx->objectPrototype(), "hasOwnProperty");
    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    ctx->setProperty(w, "foo", ctx->createNumber(2));
    Value* args[1] = {ctx->createString("foo")};
    EXPECT_TRUE(boolOf(ctx->call(hasOwn, w, 1, args)));
}

// ===== String 包装的 length 与索引 =====

TEST(PrimitiveWrapperTest, StringWrapperLengthAndIndex) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("abc"));
    EXPECT_EQ(numOf(ctx->getProperty(w, "length")), 3.0);
    EXPECT_EQ(strOf(ctx->getProperty(w, "0")), "a");
    EXPECT_EQ(strOf(ctx->getProperty(w, "2")), "c");
    EXPECT_EQ(ctx->getProperty(w, "3")->type()->kind(), TypeKind::Undefined);
    EXPECT_TRUE(neo::test::hasProperty(ctx, w, "length"));
    EXPECT_TRUE(neo::test::hasProperty(ctx, w, "1"));
    EXPECT_FALSE(neo::test::hasProperty(ctx, w, "3"));
}

TEST(PrimitiveWrapperTest, StringWrapperLengthAndIndexAreReadOnly) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("abc"));
    EXPECT_EQ(ctx->setProperty(w, "length", ctx->createNumber(1))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->setProperty(w, "0", ctx->createString("z"))->type()->kind(),
              TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(w, "0")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->deleteProperty(w, "length")->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->defineProperty(w, "0", ctx->createString("z"))->type()->kind(),
              TypeKind::Exception);
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    EXPECT_EQ(ctx->defineAccessor(w, "0", getter, nullptr)->type()->kind(),
              TypeKind::Exception);
    // 原值不变
    EXPECT_EQ(strOf(ctx->getProperty(w, "0")), "a");
}

TEST(PrimitiveWrapperTest, NonStringWrapperHasNoLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    EXPECT_EQ(ctx->getProperty(w, "length")->type()->kind(), TypeKind::Undefined);
    EXPECT_FALSE(neo::test::hasProperty(ctx, w, "length"));
}

TEST(PrimitiveWrapperTest, StringWrapperAllowsNamedProperties) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("abc"));
    ctx->setProperty(w, "foo", ctx->createNumber(9));
    EXPECT_EQ(numOf(ctx->getProperty(w, "foo")), 9.0);
    EXPECT_EQ(ctx->deleteProperty(w, "foo")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, w, "foo"));
}

// ===== 原始值属性访问的 ToObject（ES2024 §7.1.18）=====

TEST(PrimitiveWrapperTest, StringPrimitivePropertyAccessUsesWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* s = ctx->createString("abc");
    EXPECT_EQ(numOf(ctx->getProperty(s, "length")), 3.0);
    EXPECT_EQ(strOf(ctx->getProperty(s, "0")), "a");
    // 原型方法可经原始值访问
    Value* toStringFn = ctx->getProperty(s, "toString");
    EXPECT_EQ(toStringFn->type()->kind(), TypeKind::Function);
}

TEST(PrimitiveWrapperTest, NumberPrimitivePropertyAccessUsesWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* n = ctx->createNumber(1);
    Value* valueOf = ctx->getProperty(n, "valueOf");
    ASSERT_EQ(valueOf->type()->kind(), TypeKind::Function);
    EXPECT_DOUBLE_EQ(numOf(ctx->call(valueOf, n, 0, nullptr)), 1.0);
}

TEST(PrimitiveWrapperTest, BooleanPrimitivePropertyAccessUsesWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* b = ctx->createBoolean(true);
    Value* toStringFn = ctx->getProperty(b, "toString");
    ASSERT_EQ(toStringFn->type()->kind(), TypeKind::Function);
    EXPECT_EQ(strOf(ctx->call(toStringFn, b, 0, nullptr)), "true");
}

TEST(PrimitiveWrapperTest, UndefinedAndNullPropertyAccessReturnsUndefined) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(ctx->getProperty(ctx->createUndefined(), "x")->type()->kind(),
              TypeKind::Undefined);
    EXPECT_EQ(ctx->getProperty(ctx->createNull(), "x")->type()->kind(),
              TypeKind::Undefined);
}

TEST(PrimitiveWrapperTest, PrimitivePropertyWriteStillRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(ctx->setProperty(ctx->createString("abc"), "x", ctx->createNumber(1))
                  ->type()->kind(),
              TypeKind::Exception);
}

// ===== 运算（ToPrimitive）=====

TEST(PrimitiveWrapperTest, ArithmeticUnwrapsPrimitive) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* n2 = ctx->createPrimitiveWrapper(ctx->createNumber(2));
    // new Number(2) + 1 === 3
    EXPECT_DOUBLE_EQ(numOf(ctx->add(n2, ctx->createNumber(1))), 3.0);
    // 1 + new Number(2) === 3
    EXPECT_DOUBLE_EQ(numOf(ctx->add(ctx->createNumber(1), n2)), 3.0);
    // new Number(10) - 4 === 6
    Value* n10 = ctx->createPrimitiveWrapper(ctx->createNumber(10));
    EXPECT_DOUBLE_EQ(numOf(ctx->sub(n10, ctx->createNumber(4))), 6.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->mul(n10, ctx->createNumber(2))), 20.0);
    EXPECT_DOUBLE_EQ(numOf(ctx->div(n10, ctx->createNumber(5))), 2.0);
}

TEST(PrimitiveWrapperTest, StringWrapperConcatenates) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* s = ctx->createPrimitiveWrapper(ctx->createString("a"));
    // 1 + new String("a") === "1a"
    EXPECT_EQ(strOf(ctx->add(ctx->createNumber(1), s)), "1a");
    // new String("a") + 1 === "a1"
    EXPECT_EQ(strOf(ctx->add(s, ctx->createNumber(1))), "a1");
}

TEST(PrimitiveWrapperTest, WrapperToStringUnwraps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(strOf(ctx->toString(ctx->createPrimitiveWrapper(ctx->createNumber(5)))), "5");
    EXPECT_EQ(strOf(ctx->toString(ctx->createPrimitiveWrapper(ctx->createBoolean(true)))),
              "true");
}

TEST(PrimitiveWrapperTest, ArrayPlusWrapperUnwraps) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* arr = ctx->createArray();
    ctx->setProperty(arr, "0", ctx->createNumber(1));
    Value* n2 = ctx->createPrimitiveWrapper(ctx->createNumber(2));
    // [1] + new Number(2) === "12"
    EXPECT_EQ(strOf(ctx->add(arr, n2)), "12");
}

TEST(PrimitiveWrapperTest, WrapperCallAndConstructRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    EXPECT_EQ(ctx->call(w, nullptr, 0, nullptr)->type()->kind(), TypeKind::Exception);
    EXPECT_EQ(ctx->construct(w, 0, nullptr)->type()->kind(), TypeKind::Exception);
}

TEST(PrimitiveWrapperTest, PassthroughOnRhsError) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    Value* exc = ctx->createException(ctx->createString("boom"));
    EXPECT_EQ(ctx->add(w, exc), exc);
    EXPECT_EQ(ctx->sub(w, exc), exc);
    EXPECT_EQ(ctx->mul(w, exc), exc);
    EXPECT_EQ(ctx->div(w, exc), exc);
}

TEST(PrimitiveWrapperTest, ConstructorHooksCoverNoArgAndErrorPaths) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    // new Boolean() 无参 → false 包装
    Value* bw = ctx->construct(globalOf(ctx.get(), "Boolean"), 0, nullptr);
    ASSERT_EQ(bw->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_FALSE(boolOf(wrapperPrimitiveValue(bw)));

    // new Number() 无参 → 0 包装
    Value* nw = ctx->construct(globalOf(ctx.get(), "Number"), 0, nullptr);
    ASSERT_EQ(nw->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_DOUBLE_EQ(numOf(wrapperPrimitiveValue(nw)), 0.0);

    // new String() 无参 → "" 包装
    Value* sw = ctx->construct(globalOf(ctx.get(), "String"), 0, nullptr);
    ASSERT_EQ(sw->type()->kind(), TypeKind::PrimitiveWrapper);
    EXPECT_EQ(strOf(wrapperPrimitiveValue(sw)), "");
}

TEST(PrimitiveWrapperTest, WrapperHelpersRejectNonWrapper) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    EXPECT_EQ(wrapperPrimitiveValue(nullptr), nullptr);
    EXPECT_EQ(wrapperPrimitiveValue(ctx->createNumber(1)), nullptr);
    EXPECT_EQ(neo::core::wrapperStringValue(nullptr), nullptr);
    EXPECT_EQ(neo::core::wrapperStringValue(ctx->createNumber(1)), nullptr);
    EXPECT_EQ(neo::core::wrapperStringValue(
                  ctx->createPrimitiveWrapper(ctx->createNumber(1))), nullptr);
    const std::string* s = neo::core::wrapperStringValue(
        ctx->createPrimitiveWrapper(ctx->createString("ok")));
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, "ok");
}

TEST(PrimitiveWrapperTest, SymbolWrapperHasNoStringLength) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createSymbol("s"));
    EXPECT_EQ(ctx->getProperty(w, "length")->type()->kind(), TypeKind::Undefined);
    EXPECT_EQ(ctx->getProperty(w, "0")->type()->kind(), TypeKind::Undefined);
}

TEST(PrimitiveWrapperTest, SymbolKeySkipsIndexParsing) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("abc"));
    Value* sym = ctx->createSymbol("k");
    ctx->setProperty(w, sym, ctx->createNumber(5));
    EXPECT_EQ(numOf(ctx->getProperty(w, sym)), 5.0);
    EXPECT_TRUE(neo::test::hasProperty(ctx, w, sym));
    EXPECT_EQ(ctx->deleteProperty(w, sym)->type()->kind(), TypeKind::Boolean);
}

TEST(PrimitiveWrapperTest, StringWrapperIndexBeyondLengthFallsThrough) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("ab"));
    // 越界索引不是只读属性，可正常写入
    Value* one = ctx->createNumber(1);
    EXPECT_EQ(ctx->setProperty(w, "5", one), one);
    EXPECT_EQ(numOf(ctx->getProperty(w, "5")), 1.0);
    // 越界索引也可删除
    EXPECT_EQ(ctx->deleteProperty(w, "5")->type()->kind(), TypeKind::Boolean);
}

TEST(PrimitiveWrapperTest, StringWrapperNonIndexNamedAccessorWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("ab"));
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(9); }, 0);
    EXPECT_EQ(ctx->defineAccessor(w, "x", getter, nullptr), w);
    EXPECT_EQ(numOf(ctx->getProperty(w, "x")), 9.0);
}

TEST(PrimitiveWrapperTest, NonStringWrapperPropertyOpsDelegateToObject) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    // defineProperty 走 ObjectType
    ctx->defineProperty(w, "a", ctx->createNumber(1));
    EXPECT_EQ(numOf(ctx->getProperty(w, "a")), 1.0);
    // defineAccessor 走 ObjectType
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(3); }, 0);
    EXPECT_EQ(ctx->defineAccessor(w, "b", getter, nullptr), w);
    EXPECT_EQ(numOf(ctx->getProperty(w, "b")), 3.0);
    // delete 走 ObjectType
    EXPECT_EQ(ctx->deleteProperty(w, "a")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, w, "a"));
}

TEST(PrimitiveWrapperTest, StringWrapperNonIndexDefineAndDeleteDelegate) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("ab"));
    // 非索引键的 defineProperty / defineAccessor / delete 走 ObjectType
    ctx->defineProperty(w, "n", ctx->createNumber(4));
    EXPECT_EQ(numOf(ctx->getProperty(w, "n")), 4.0);
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createNumber(5); }, 0);
    EXPECT_EQ(ctx->defineAccessor(w, "g", getter, nullptr), w);
    EXPECT_EQ(numOf(ctx->getProperty(w, "g")), 5.0);
    EXPECT_EQ(ctx->deleteProperty(w, "n")->type()->kind(), TypeKind::Boolean);
    EXPECT_FALSE(neo::test::hasProperty(ctx, w, "n"));
    // 越界索引的 defineProperty 也走 ObjectType
    ctx->defineProperty(w, "9", ctx->createNumber(9));
    EXPECT_EQ(numOf(ctx->getProperty(w, "9")), 9.0);
    // 越界索引的 defineAccessor 也走 ObjectType
    EXPECT_EQ(ctx->defineAccessor(w, "8", getter, nullptr), w);
    EXPECT_EQ(numOf(ctx->getProperty(w, "8")), 5.0);
}

TEST(PrimitiveWrapperTest, StringWrapperDefineAccessorOnLengthRejected) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("ab"));
    Value* getter = ctx->createNativeFunction(
        [](IContext* c, Value*, size_t, Value*[]) { return c->createUndefined(); }, 0);
    EXPECT_EQ(ctx->defineAccessor(w, "length", getter, nullptr)->type()->kind(),
              TypeKind::Exception);
}

// ===== GC 与 clone =====

TEST(PrimitiveWrapperTest, EnumerateChildrenVisitsPrimitiveValue) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    CountingVisitor visitor;
    dataOf(w)->enumerateChildren(&visitor);
    // prototype + [[PrimitiveValue]]
    EXPECT_EQ(visitor.count, 2);
}

TEST(PrimitiveWrapperTest, PrimitiveValueAliveThroughWrapperDuringGc) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createString("keep"));
    ctx->gc()->collect();
    EXPECT_EQ(strOf(wrapperPrimitiveValue(w)), "keep");
}

TEST(PrimitiveWrapperTest, CloneSharesData) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    auto* d = dataOf(w);
    int before = d->ref();
    auto* cloned = static_cast<PrimitiveWrapperData*>(d->clone(alloc.get()));
    EXPECT_EQ(cloned, d);
    EXPECT_EQ(d->ref(), before + 1);
    d->refDec();
}

TEST(PrimitiveWrapperTest, SetPrototypeWorks) {
    auto alloc = createMallocAllocator();
    NullSink sink;
    Console console(&sink);
    auto ctx = createContext(alloc.get(), &console);

    Value* w = ctx->createPrimitiveWrapper(ctx->createNumber(1));
    Value* proto = ctx->createObject();
    EXPECT_EQ(ctx->setPrototype(w, proto), w);
    EXPECT_EQ(ctx->getPrototype(w), proto);
}
