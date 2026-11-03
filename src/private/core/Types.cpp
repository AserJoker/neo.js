// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 IType* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
// 各 Type/Data 子类拆分到 types/ 子目录独立文件（每类型一个头文件，Type+Data 同文件内聚），
// 防止单文件随行为方法实现膨胀。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/types/Boolean.h"
#include "core/types/Exception.h"
#include "core/types/Fatal.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/Undefined.h"

namespace neo::core {

IType* get_undefined_type() {
    static UndefinedType inst;
    return &inst;
}

IType* get_null_type() {
    static NullType inst;
    return &inst;
}

IType* get_boolean_type() {
    static BooleanType inst;
    return &inst;
}

IType* get_number_type() {
    static NumberType inst;
    return &inst;
}

IType* get_string_type() {
    static StringType inst;
    return &inst;
}

IType* get_exception_type() {
    static ExceptionType inst;
    return &inst;
}

IType* get_fatal_type() {
    static FatalType inst;
    return &inst;
}

}  // namespace neo::core
