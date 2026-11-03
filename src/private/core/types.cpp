// types.cpp - Type 单例实现
//
// 每种类型一个全局单例（函数内 static），Value 持 Type* 共享，无须引用计数。
// 单例生命周期与进程一致，GC 不跟踪 Type。
//
// 关联 ADR: 016（Type 体系修订）

#include "core/impl_types.h"

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
