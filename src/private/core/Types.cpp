// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 IType* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
// 各 Type/Data 子类拆分到 types/ 子目录独立文件（每类型一个头文件，Type+Data 同文件内聚），
// 防止单文件随行为方法实现膨胀。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/types/Array.h"
#include "core/types/AsyncGenerator.h"
#include "core/types/Boolean.h"
#include "core/types/Coroutine.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Interrupt.h"
#include "core/types/Iterator.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/ModuleNamespace.h"
#include "core/types/Object.h"
#include "core/types/Promise.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Proxy.h"
#include "core/types/String.h"
#include "core/types/Symbol.h"
#include "core/types/Undefined.h"
#include "core/types/Uninitialized.h"

namespace neo::core {

IType* getUndefinedType() {
    static UndefinedType inst;
    return &inst;
}

IType* getNullType() {
    static NullType inst;
    return &inst;
}

IType* getBooleanType() {
    static BooleanType inst;
    return &inst;
}

IType* getNumberType() {
    static NumberType inst;
    return &inst;
}

IType* getStringType() {
    static StringType inst;
    return &inst;
}

IType* getExceptionType() {
    static ExceptionType inst;
    return &inst;
}

IType* getFatalType() {
    static FatalType inst;
    return &inst;
}

IType* getUninitializedType() {
    static UninitializedType inst;
    return &inst;
}

IType* getFunctionType() {
    static FunctionType inst;
    return &inst;
}

IType* getObjectType() {
    static ObjectType inst;
    return &inst;
}

IType* getArrayType() {
    static ArrayType inst;
    return &inst;
}

IType* getSymbolType() {
    static SymbolType inst;
    return &inst;
}

IType* getPrimitiveWrapperType() {
    static PrimitiveWrapperType inst;
    return &inst;
}

IType* getArrayIteratorType() {
    static ArrayIteratorType inst;
    return &inst;
}

IType* getIteratorWrapperType() {
    static IteratorWrapperType inst;
    return &inst;
}

IType* getErrorType() {
    static ErrorType inst;
    return &inst;
}

IType* getPromiseType() {
    static PromiseType inst;
    return &inst;
}

IType* getInterruptType() {
    static InterruptType inst;
    return &inst;
}

IType* getCoroutineType() {
    static CoroutineType inst;
    return &inst;
}

IType* getAsyncGeneratorType() {
    static AsyncGeneratorType inst;
    return &inst;
}

IType* getModuleNamespaceType() {
    static ModuleNamespaceType inst;
    return &inst;
}

IType* getProxyType() {
    static ProxyType inst;
    return &inst;
}

// TypeKind → Type 单例映射（唯一真相源）。
// 新增类型：在此加一行 case + 上面的 getXxxType()，对象模型分派点自动覆盖。
IType* getTypeByKind(TypeKind k) {
    switch (k) {
        case TypeKind::Undefined:        return getUndefinedType();
        case TypeKind::Null:             return getNullType();
        case TypeKind::Boolean:          return getBooleanType();
        case TypeKind::Number:           return getNumberType();
        case TypeKind::String:           return getStringType();
        case TypeKind::Exception:        return getExceptionType();
        case TypeKind::Fatal:            return getFatalType();
        case TypeKind::Uninitialized:    return getUninitializedType();
        case TypeKind::Function:         return getFunctionType();
        case TypeKind::Object:           return getObjectType();
        case TypeKind::Array:            return getArrayType();
        case TypeKind::Symbol:           return getSymbolType();
        case TypeKind::PrimitiveWrapper: return getPrimitiveWrapperType();
        case TypeKind::ArrayIterator:    return getArrayIteratorType();
        case TypeKind::IteratorWrapper:  return getIteratorWrapperType();
        case TypeKind::Error:            return getErrorType();
        case TypeKind::Promise:          return getPromiseType();
        case TypeKind::Interrupt:        return getInterruptType();
        case TypeKind::Coroutine:        return getCoroutineType();
        case TypeKind::AsyncGenerator:   return getAsyncGeneratorType();
        case TypeKind::ModuleNamespace:  return getModuleNamespaceType();
        case TypeKind::Proxy:            return getProxyType();
    }
    return nullptr;
}

bool kindHasObjectLayout(TypeKind k) {
    IType* t = getTypeByKind(k);
    return t != nullptr && t->hasObjectLayout();
}

}  // namespace neo::core
