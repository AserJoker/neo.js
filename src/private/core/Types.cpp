// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 IType* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
// 各 Type/Data 子类拆分到 types/ 子目录独立文件（每类型一个头文件，Type+Data 同文件内聚），
// 防止单文件随行为方法实现膨胀。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/types/Arguments.h"
#include "core/types/Array.h"
#include "core/types/AsyncGenerator.h"
#include "core/types/BigInt.h"
#include "core/types/Boolean.h"
#include "core/types/Collection.h"
#include "core/types/Date.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/DataView.h"
#include "core/types/TypedArray.h"
#include "core/types/Coroutine.h"
#include "core/types/Error.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Interrupt.h"
#include "core/types/IntlCollator.h"
#include "core/types/IntlDateTimeFormat.h"
#include "core/types/IntlPluralRules.h"
#include "core/types/IntlRelativeTimeFormat.h"
#include "core/types/IntlListFormat.h"
#include "core/types/IntlDisplayNames.h"
#include "core/types/IntlSegmenter.h"
#include "core/types/IntlSegments.h"
#include "core/types/IntlLocale.h"
#include "core/types/Worker.h"
#include "core/types/MessagePort.h"
#include "core/types/IntlNumberFormat.h"
#include "core/types/Iterator.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/ModuleNamespace.h"
#include "core/types/Object.h"
#include "core/types/Promise.h"
#include "core/types/PrimitiveWrapper.h"
#include "core/types/Proxy.h"
#include "core/types/RegExp.h"
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

IType* getBigIntType() {
    static BigIntType inst;
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

IType* getStringIteratorType() {
    static StringIteratorType inst;
    return &inst;
}

IType* getIteratorWrapperType() {
    static IteratorWrapperType inst;
    return &inst;
}

IType* getIteratorHelperType() {
    static IteratorHelperType inst;
    return &inst;
}

IType* getCollectionType() {
    static CollectionType inst;
    return &inst;
}

IType* getCollectionIteratorType() {
    static CollectionIteratorType inst;
    return &inst;
}

IType* getDateType() {
    static DateType inst;
    return &inst;
}

IType* getArrayBufferType() {
    static ArrayBufferType inst;
    return &inst;
}

IType* getDataViewType() {
    static DataViewType inst;
    return &inst;
}

IType* getTypedArrayType() {
    static TypedArrayType inst;
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

IType* getRegExpType() {
    static RegExpType inst;
    return &inst;
}

IType* getIntlNumberFormatType() {
    static IntlNumberFormatType inst;
    return &inst;
}

IType* getIntlDateTimeFormatType() {
    static IntlDateTimeFormatType inst;
    return &inst;
}

IType* getIntlCollatorType() {
    static IntlCollatorType inst;
    return &inst;
}

IType* getIntlPluralRulesType() {
    static IntlPluralRulesType inst;
    return &inst;
}

IType* getIntlRelativeTimeFormatType() {
    static IntlRelativeTimeFormatType inst;
    return &inst;
}

IType* getIntlListFormatType() {
    static IntlListFormatType inst;
    return &inst;
}

IType* getIntlDisplayNamesType() {
    static IntlDisplayNamesType inst;
    return &inst;
}

IType* getIntlSegmenterType() {
    static IntlSegmenterType inst;
    return &inst;
}

IType* getIntlSegmentsType() {
    static IntlSegmentsType inst;
    return &inst;
}

IType* getIntlLocaleType() {
    static IntlLocaleType inst;
    return &inst;
}

IType* getWorkerType() {
    static WorkerType inst;
    return &inst;
}

IType* getMessagePortType() {
    static MessagePortType inst;
    return &inst;
}

IType* getRegExpStringIteratorType() {
    static RegExpStringIteratorType inst;
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
        case TypeKind::BigInt:           return getBigIntType();
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
        case TypeKind::StringIterator:   return getStringIteratorType();
        case TypeKind::IteratorWrapper:  return getIteratorWrapperType();
        case TypeKind::IteratorHelper:   return getIteratorHelperType();
        case TypeKind::Collection:       return getCollectionType();
        case TypeKind::CollectionIterator: return getCollectionIteratorType();
        case TypeKind::Date:             return getDateType();
        case TypeKind::ArrayBuffer:      return getArrayBufferType();
        case TypeKind::DataView:         return getDataViewType();
        case TypeKind::TypedArray:       return getTypedArrayType();
        case TypeKind::Error:            return getErrorType();
        case TypeKind::Promise:          return getPromiseType();
        case TypeKind::Interrupt:        return getInterruptType();
        case TypeKind::Coroutine:        return getCoroutineType();
        case TypeKind::AsyncGenerator:   return getAsyncGeneratorType();
        case TypeKind::ModuleNamespace:  return getModuleNamespaceType();
        case TypeKind::Proxy:            return getProxyType();
        case TypeKind::RegExp:           return getRegExpType();
        case TypeKind::RegExpStringIterator:
            return getRegExpStringIteratorType();
        case TypeKind::Arguments:        return getArgumentsType();
        case TypeKind::IntlNumberFormat: return getIntlNumberFormatType();
        case TypeKind::IntlDateTimeFormat: return getIntlDateTimeFormatType();
        case TypeKind::IntlCollator: return getIntlCollatorType();
        case TypeKind::IntlPluralRules: return getIntlPluralRulesType();
        case TypeKind::IntlRelativeTimeFormat:
            return getIntlRelativeTimeFormatType();
        case TypeKind::IntlListFormat: return getIntlListFormatType();
        case TypeKind::IntlDisplayNames: return getIntlDisplayNamesType();
        case TypeKind::IntlSegmenter: return getIntlSegmenterType();
        case TypeKind::IntlSegments: return getIntlSegmentsType();
        case TypeKind::IntlLocale: return getIntlLocaleType();
        case TypeKind::Worker: return getWorkerType();
        case TypeKind::MessagePort: return getMessagePortType();
    }
    return nullptr;
}

bool kindHasObjectLayout(TypeKind k) {
    IType* t = getTypeByKind(k);
    return t != nullptr && t->hasObjectLayout();
}

}  // namespace neo::core
