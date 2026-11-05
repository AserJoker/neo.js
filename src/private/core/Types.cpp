// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 IType* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
// 各 Type/Data 子类拆分到 types/ 子目录独立文件（每类型一个头文件，Type+Data 同文件内聚），
// 防止单文件随行为方法实现膨胀。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Function.h"
#include "core/types/Null.h"
#include "core/types/Object.h"
#include "core/types/Number.h"
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

}  // namespace neo::core
