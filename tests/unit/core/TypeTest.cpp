// type_test.cpp - Type 体系与 Data 子类单元测试
//
// 验证：
// - TypeKind 枚举与 name() 对应
// - Type 单例（同类型多次获取返回同一指针）
// - Data 子类负载正确（BooleanData/NumberData/StringData/FatalData）
// - StringData 不可变性（构造后内容确定）
// - ExceptionData::enumerateChildren 向 visitor 报告 child（GC 引用链）
// - FatalData 持 const char* 字面量
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）、015（data 引用计数分层）

#include "core/IGCEngine.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Undefined.h"
#include "core/types/Uninitialized.h"
#include "infra/IAllocator.h"
#include "infra/MallocAllocator.h"

#include <gtest/gtest.h>

#include <new>
#include <string>

namespace {

using neo::core::BooleanData;
using neo::core::IData;
using neo::core::ExceptionData;
using neo::core::FatalData;
using neo::core::IGCVisitor;
using neo::core::NullData;
using neo::core::NumberData;
using neo::core::StringData;
using neo::core::IType;
using neo::core::TypeKind;
using neo::core::UndefinedData;
using neo::core::UninitializedData;
using neo::core::Value;
using neo::core::getBooleanType;
using neo::core::getExceptionType;
using neo::core::getFatalType;
using neo::core::getNullType;
using neo::core::getNumberType;
using neo::core::getStringType;
using neo::core::getUndefinedType;
using neo::core::getUninitializedType;
using neo::infra::MallocAllocator;

// ---- TypeKind 与 name ----

TEST(TypeTest, KindAndName) {
    EXPECT_EQ(getUndefinedType()->kind(), TypeKind::Undefined);
    EXPECT_STREQ(getUndefinedType()->name(), "undefined");

    EXPECT_EQ(getNullType()->kind(), TypeKind::Null);
    EXPECT_STREQ(getNullType()->name(), "null");

    EXPECT_EQ(getBooleanType()->kind(), TypeKind::Boolean);
    EXPECT_STREQ(getBooleanType()->name(), "boolean");

    EXPECT_EQ(getNumberType()->kind(), TypeKind::Number);
    EXPECT_STREQ(getNumberType()->name(), "number");

    EXPECT_EQ(getStringType()->kind(), TypeKind::String);
    EXPECT_STREQ(getStringType()->name(), "string");

    EXPECT_EQ(getExceptionType()->kind(), TypeKind::Exception);
    EXPECT_STREQ(getExceptionType()->name(), "exception");

    EXPECT_EQ(getFatalType()->kind(), TypeKind::Fatal);
    EXPECT_STREQ(getFatalType()->name(), "fatal");

    EXPECT_EQ(getUninitializedType()->kind(), TypeKind::Uninitialized);
    EXPECT_STREQ(getUninitializedType()->name(), "uninitialized");
}

// ---- 单例：同类型多次获取返回同一指针 ----

TEST(TypeTest, SingletonIdentity) {
    EXPECT_EQ(getUndefinedType(), getUndefinedType());
    EXPECT_EQ(getNullType(), getNullType());
    EXPECT_EQ(getBooleanType(), getBooleanType());
    EXPECT_EQ(getNumberType(), getNumberType());
    EXPECT_EQ(getStringType(), getStringType());
    EXPECT_EQ(getExceptionType(), getExceptionType());
    EXPECT_EQ(getFatalType(), getFatalType());
    EXPECT_EQ(getUninitializedType(), getUninitializedType());
}

// ---- 不同类型单例互不相同 ----

TEST(TypeTest, DistinctTypes) {
    IType* types[] = {
        getUndefinedType(), getNullType(),    getBooleanType(),
        getNumberType(),    getStringType(),  getExceptionType(),
        getFatalType(),     getUninitializedType(),
    };
    for (size_t i = 0; i < std::size(types); ++i) {
        for (size_t j = i + 1; j < std::size(types); ++j) {
            EXPECT_NE(types[i], types[j]);
        }
    }
}

// ---- Data 子类负载 ----

TEST(DataTest, UndefinedAndNullHaveNoPayload) {
    UndefinedData ud;
    NullData nd;
    // 空负载 data，ref 从 0 开始
    EXPECT_EQ(ud.ref(), 0);
    EXPECT_EQ(nd.ref(), 0);
}

TEST(DataTest, BooleanDataHoldsBool) {
    BooleanData t(true);
    BooleanData f(false);
    EXPECT_TRUE(t.value);
    EXPECT_FALSE(f.value);
}

TEST(DataTest, NumberDataHoldsDouble) {
    NumberData n(3.14);
    EXPECT_DOUBLE_EQ(n.value, 3.14);
}

TEST(DataTest, StringDataHoldsString) {
    StringData s(std::string("hello"));
    EXPECT_EQ(s.value, "hello");
}

TEST(DataTest, StringDataHoldsStringView) {
    StringData s(std::string_view("world"));
    EXPECT_EQ(s.value, "world");
}

// ---- StringData 不可变性：构造后内容确定 ----
// （不可变性是接口契约：调用方不应修改 s.value，"修改"操作返回新 StringData。
//   此测试验证构造时内容正确，不验证运行时防篡改——那是接口纪律非语言机制。）

TEST(DataTest, StringDataContentFixedAtConstruction) {
    std::string original = "immutable";
    StringData s(original);
    original[0] = 'X';  // 修改源字符串不影响已构造的 StringData
    EXPECT_EQ(s.value, "immutable");
}

// ---- FatalData 持 const char* 字面量 ----

TEST(DataTest, FatalDataHoldsLiteral) {
    FatalData fd("OOM");
    EXPECT_STREQ(fd.message, "OOM");
}

TEST(DataTest, FatalDataHoldsAnotherLiteral) {
    FatalData fd("stack overflow");
    EXPECT_STREQ(fd.message, "stack overflow");
}

// ---- ExceptionData::enumerateChildren 报告 child ----
// 用计数 visitor 验证 child 被报告给 GC

class CountingVisitor : public IGCVisitor {
public:
    int count = 0;
    Value* visited = nullptr;
    void visit(Value* v) override {
        ++count;
        visited = v;
    }
};

TEST(ExceptionDataTest, EnumerateChildrenReportsChild) {
    MallocAllocator alloc;
    // 构造一个 child Value（用 NumberData 作负载，dummy type 单例）
    auto* d = new (alloc.alloc(sizeof(NumberData))) NumberData(42.0);
    auto* child = new (alloc.alloc(sizeof(Value))) Value(getNumberType(), d);

    ExceptionData ed(child);
    CountingVisitor visitor;
    ed.enumerateChildren(&visitor);

    EXPECT_EQ(visitor.count, 1);
    EXPECT_EQ(visitor.visited, child);

    // 清理
    child->~Value();
    alloc.free(child);
    alloc.free(d);
}

TEST(ExceptionDataTest, EnumerateChildrenReportsChildOnce) {
    MallocAllocator alloc;
    auto* d = new (alloc.alloc(sizeof(NumberData))) NumberData(0.0);
    auto* child = new (alloc.alloc(sizeof(Value))) Value(getNumberType(), d);

    ExceptionData ed(child);
    CountingVisitor visitor;
    ed.enumerateChildren(&visitor);
    ed.enumerateChildren(&visitor);  // 调两次，每次都报告

    EXPECT_EQ(visitor.count, 2);

    child->~Value();
    alloc.free(child);
    alloc.free(d);
}

// ---- ExceptionData child 不为 nullptr（不携带错误对象用 undefined Value）----
// 此契约由调用方（Context）保证：构造 ExceptionData 时必须传入有效 Value*。
// 不携带错误对象时传入 undefined Value，而非 nullptr。

TEST(ExceptionDataTest, ChildMustNotBeNull) {
    MallocAllocator alloc;
    // 不携带错误对象：用 undefined Value（而非 nullptr）
    auto* ud = new (alloc.alloc(sizeof(UndefinedData))) UndefinedData();
    auto* undefined_val = new (alloc.alloc(sizeof(Value))) Value(getUndefinedType(), ud);

    ExceptionData ed(undefined_val);
    EXPECT_NE(ed.child, nullptr);
    EXPECT_EQ(ed.child->type()->kind(), TypeKind::Undefined);

    CountingVisitor visitor;
    ed.enumerateChildren(&visitor);
    EXPECT_EQ(visitor.count, 1);

    undefined_val->~Value();
    alloc.free(undefined_val);
    alloc.free(ud);
}

// ---- TypeKind → Type 单例映射（getTypeByKind 唯一真相源）----

TEST(TypeTest, GetTypeByKindRoundTrips) {
    const TypeKind kinds[] = {
        TypeKind::Undefined, TypeKind::Null,       TypeKind::Boolean,
        TypeKind::Number,    TypeKind::String,     TypeKind::Exception,
        TypeKind::Fatal,     TypeKind::Uninitialized,
    };
    for (TypeKind k : kinds) {
        IType* t = neo::core::getTypeByKind(k);
        ASSERT_NE(t, nullptr);
        EXPECT_EQ(t->kind(), k);
    }
}

// ---- 对象布局自声明（IType::hasObjectLayout / kindHasObjectLayout）----

TEST(TypeTest, KindHasObjectLayoutForPrimitivesIsFalse) {
    const TypeKind primitives[] = {
        TypeKind::Undefined, TypeKind::Null,    TypeKind::Boolean,
        TypeKind::Number,    TypeKind::String,  TypeKind::Exception,
        TypeKind::Fatal,     TypeKind::Uninitialized, TypeKind::Symbol,
    };
    for (TypeKind k : primitives) {
        EXPECT_FALSE(neo::core::kindHasObjectLayout(k)) << "kind=" << static_cast<int>(k);
        EXPECT_FALSE(neo::core::getTypeByKind(k)->hasObjectLayout());
    }
}

TEST(TypeTest, KindHasObjectLayoutForObjectsIsTrue) {
    const TypeKind objects[] = {
        TypeKind::Object,          TypeKind::Function,
        TypeKind::Array,           TypeKind::PrimitiveWrapper,
        TypeKind::ArrayIterator,   TypeKind::IteratorWrapper,
    };
    for (TypeKind k : objects) {
        EXPECT_TRUE(neo::core::kindHasObjectLayout(k)) << "kind=" << static_cast<int>(k);
        EXPECT_TRUE(neo::core::getTypeByKind(k)->hasObjectLayout());
    }
}

// ---- 索引型 exotic 能力默认实现（非索引类型返回空）----

TEST(TypeTest, NonIndexedTypesHaveNoIndexedElements) {
    IType* t = getNumberType();
    EXPECT_FALSE(t->hasIndexedElements());
    EXPECT_EQ(t->indexedLength(nullptr), 0u);
    EXPECT_EQ(t->indexedElement(nullptr, nullptr, 0), nullptr);
}

// ---- Uninitialized（TDZ 哨兵）----

TEST(UninitializedTest, DataHasNoPayload) {
    UninitializedData ud;
    EXPECT_EQ(ud.ref(), 0);
}

TEST(UninitializedTest, CloneProducesDistinctObject) {
    MallocAllocator alloc;
    UninitializedData src;
    IData* cloned = src.clone(&alloc);
    ASSERT_NE(cloned, nullptr);
    EXPECT_NE(cloned, &src);
    alloc.free(cloned);
}

TEST(UninitializedTest, AllBehaviorMethodsPassthroughSelf) {
    MallocAllocator alloc;
    auto* d1 = new (alloc.alloc(sizeof(UninitializedData))) UninitializedData();
    auto* self = new (alloc.alloc(sizeof(Value))) Value(getUninitializedType(), d1);
    auto* d2 = new (alloc.alloc(sizeof(NumberData))) NumberData(1.0);
    auto* other = new (alloc.alloc(sizeof(Value))) Value(getNumberType(), d2);

    IType* t = getUninitializedType();
    // 全透传 self（TDZ 哨兵不参与运算，进入前应已失败）
    EXPECT_EQ(t->add(nullptr, self, other), self);
    EXPECT_EQ(t->sub(nullptr, self, other), self);
    EXPECT_EQ(t->mul(nullptr, self, other), self);
    EXPECT_EQ(t->div(nullptr, self, other), self);
    EXPECT_EQ(t->toString(nullptr, self), self);

    other->~Value();
    alloc.free(other);
    alloc.free(d2);
    self->~Value();
    alloc.free(self);
    alloc.free(d1);
}

}  // namespace
