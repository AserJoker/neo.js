// types/string.h - StringType + StringData
//
// string 类型：持 std::string 负载，不可变（构造后不再修改，"修改"操作返回新 StringData）。
// 行为方法（concat/slice/indexOf/to_string/...）待 Context 引入后补充——预计本文件
// 会随 StringType 行为实现大量膨胀。
//
// 关联 ADR: 016（Type 体系修订）、017（类型清单）

#ifndef NEO_CORE_PRIVATE_TYPES_STRING_H
#define NEO_CORE_PRIVATE_TYPES_STRING_H

#include "core/data.h"
#include "core/type.h"

#include <string>
#include <string_view>

namespace neo::core {

class StringType : public Type {
public:
    StringType() : Type(TypeKind::String) {}
    const char* name() const override { return "string"; }
};

// 持 std::string 的 data，不可变（构造后不再修改）
// 任何"修改"操作（concat/replace/...）返回新 StringData。
class StringData : public Data {
public:
    std::string value;
    explicit StringData(std::string v) : value(std::move(v)) {}
    explicit StringData(std::string_view v) : value(v) {}
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_STRING_H
