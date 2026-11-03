// impl_types.h (private) - 具体 Type 实现类声明
//
// 本头仅在 core 模块内部可见（src/private/）。每种 JS 类型一个 Type 子类，
// 全局单例。本阶段只实现 kind + name，行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_IMPL_TYPES_H
#define NEO_CORE_PRIVATE_IMPL_TYPES_H

#include "core/type.h"

namespace neo::core {

class UndefinedType : public Type {
public:
    UndefinedType() : Type(TypeKind::Undefined) {}
    const char* name() const override { return "undefined"; }
};

class NullType : public Type {
public:
    NullType() : Type(TypeKind::Null) {}
    const char* name() const override { return "null"; }
};

class BooleanType : public Type {
public:
    BooleanType() : Type(TypeKind::Boolean) {}
    const char* name() const override { return "boolean"; }
};

class NumberType : public Type {
public:
    NumberType() : Type(TypeKind::Number) {}
    const char* name() const override { return "number"; }
};

class StringType : public Type {
public:
    StringType() : Type(TypeKind::String) {}
    const char* name() const override { return "string"; }
};

class ExceptionType : public Type {
public:
    ExceptionType() : Type(TypeKind::Exception) {}
    const char* name() const override { return "exception"; }
};

class FatalType : public Type {
public:
    FatalType() : Type(TypeKind::Fatal) {}
    const char* name() const override { return "fatal"; }
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_IMPL_TYPES_H
