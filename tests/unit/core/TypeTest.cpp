// type_test.cpp - Type 体系与 Data 子类单元测试
//
// 验证：
// - TypeKind 枚举与 name() 对应
// - Type 单例（同类型多次获取返回同一指针）
// - Data 子类负载正确（BooleanData/NumberData/StringData/FatalData）
// - StringData 不可变性（构造后内容确定）
// - ExceptionData::enumerate_children 向 visitor 报告 child（GC 引用链）
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
using neo::core::Value;
using neo::core::get_boolean_type;
using neo::core::get_exception_type;
using neo::core::get_fatal_type;
using neo::core::get_null_type;
using neo::core::get_number_type;
using neo::core::get_string_type;
using neo::core::get_undefined_type;
using neo::infra::MallocAllocator;

// ---- TypeKind 与 name ----

TEST(TypeTest, KindAndName) {
    EXPECT_EQ(get_undefined_type()->kind(), TypeKind::Undefined);
    EXPECT_STREQ(get_undefined_type()->name(), "undefined");

    EXPECT_EQ(get_null_type()->kind(), TypeKind::Null);
    EXPECT_STREQ(get_null_type()->name(), "null");

    EXPECT_EQ(get_boolean_type()->kind(), TypeKind::Boolean);
    EXPECT_STREQ(get_boolean_type()->name(), "boolean");

    EXPECT_EQ(get_number_type()->kind(), TypeKind::Number);
    EXPECT_STREQ(get_number_type()->name(), "number");

    EXPECT_EQ(get_string_type()->kind(), TypeKind::String);
    EXPECT_STREQ(get_string_type()->name(), "string");

    EXPECT_EQ(get_exception_type()->kind(), TypeKind::Exception);
    EXPECT_STREQ(get_exception_type()->name(), "exception");

    EXPECT_EQ(get_fatal_type()->kind(), TypeKind::Fatal);
    EXPECT_STREQ(get_fatal_type()->name(), "fatal");
}

// ---- 单例：同类型多次获取返回同一指针 ----

TEST(TypeTest, SingletonIdentity) {
    EXPECT_EQ(get_undefined_type(), get_undefined_type());
    EXPECT_EQ(get_null_type(), get_null_type());
    EXPECT_EQ(get_boolean_type(), get_boolean_type());
    EXPECT_EQ(get_number_type(), get_number_type());
    EXPECT_EQ(get_string_type(), get_string_type());
    EXPECT_EQ(get_exception_type(), get_exception_type());
    EXPECT_EQ(get_fatal_type(), get_fatal_type());
}

// ---- 不同类型单例互不相同 ----

TEST(TypeTest, DistinctTypes) {
    IType* types[] = {
        get_undefined_type(), get_null_type(),    get_boolean_type(),
        get_number_type(),    get_string_type(),  get_exception_type(),
        get_fatal_type(),
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

// ---- ExceptionData::enumerate_children 报告 child ----
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
    auto* child = new (alloc.alloc(sizeof(Value))) Value(get_number_type(), d);

    ExceptionData ed(child);
    CountingVisitor visitor;
    ed.enumerate_children(&visitor);

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
    auto* child = new (alloc.alloc(sizeof(Value))) Value(get_number_type(), d);

    ExceptionData ed(child);
    CountingVisitor visitor;
    ed.enumerate_children(&visitor);
    ed.enumerate_children(&visitor);  // 调两次，每次都报告

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
    auto* undefined_val = new (alloc.alloc(sizeof(Value))) Value(get_undefined_type(), ud);

    ExceptionData ed(undefined_val);
    EXPECT_NE(ed.child, nullptr);
    EXPECT_EQ(ed.child->type()->kind(), TypeKind::Undefined);

    CountingVisitor visitor;
    ed.enumerate_children(&visitor);
    EXPECT_EQ(visitor.count, 1);

    undefined_val->~Value();
    alloc.free(undefined_val);
    alloc.free(ud);
}

}  // namespace
