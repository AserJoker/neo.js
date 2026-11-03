// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 Type* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
// 各 Type/Data 子类拆分到 types/ 子目录独立文件（每类型一个头文件，Type+Data 同文件内聚），
// 防止单文件随行为方法实现膨胀。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/types/boolean.h"
#include "core/types/exception.h"
#include "core/types/fatal.h"
#include "core/types/null.h"
#include "core/types/number.h"
#include "core/types/string.h"
#include "core/types/undefined.h"

namespace neo::core {

Type* get_undefined_type() {
    static UndefinedType inst;
    return &inst;
}

Type* get_null_type() {
    static NullType inst;
    return &inst;
}

Type* get_boolean_type() {
    static BooleanType inst;
    return &inst;
}

Type* get_number_type() {
    static NumberType inst;
    return &inst;
}

Type* get_string_type() {
    static StringType inst;
    return &inst;
}

Type* get_exception_type() {
    static ExceptionType inst;
    return &inst;
}

Type* get_fatal_type() {
    static FatalType inst;
    return &inst;
}

}  // namespace neo::core
