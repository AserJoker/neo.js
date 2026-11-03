// types/fatal.h - FatalType + FatalData
//
// fatal 类型（ADR-017 §4）：不可捕获致命错误，持 const char* 字面量字符串（静态存储期，
// 构造时确定，硬编码如 "OOM"）。不经 IAllocator 分配，GC 无须跟踪。
// FatalData 对象本身仍经 allocData 分配（Data 基类部分），但字符串负载是字面量。
// 行为方法待 Context 引入后补充。
//
// 关联 ADR: 016（Type 体系修订）、017（Fatal 类型）

#ifndef NEO_CORE_PRIVATE_TYPES_FATAL_H
#define NEO_CORE_PRIVATE_TYPES_FATAL_H

#include "core/IData.h"
#include "core/IType.h"

namespace neo::core {

class FatalType : public IType {
public:
    FatalType() : IType(TypeKind::Fatal) {}
    const char* name() const override { return "fatal"; }
};

// 持 const char*（字面量字符串，静态存储期，无须释放）
class FatalData : public IData {
public:
    const char* message;  // 字面量字符串，静态存储期，无须释放
    explicit FatalData(const char* msg) : message(msg) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_FATAL_H
