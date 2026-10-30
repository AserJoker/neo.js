/*
 * core 层：data（引用类型实际数据）与 value（值语义实体）分层。
 *
 * 遵循 docs/scope-redesign.md §1.1/§4：
 *   - value 是值语义实体：return/传参/赋值都产生新 value（拷贝语义）。
 *   - data 是引用类型的实际数据（对象/函数/数组/字符串等）。新 value 指向
 *     同一份 data，data 用引用计数管理复用。
 *   - 变量重复引用仅两处：闭包捕获 + 模块 import。
 *   - 全部装箱：create_value 开销可接受，不内联小值。
 *   - data 的生命周期归 value 的析构函数管：value 析构时 data refcount--，
 *     归零 delete。引用计数增减发生在 value 拷贝/赋值时（create_value ++，
 *     析构 --）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_CORE_DATA_H
#define NEOJS_CORE_DATA_H

#include <cstdint>
#include <string>

#include "neojs/core/allocator.h"

namespace neojs {
namespace core {

/* data 类型（M1：值类型数据 + 预留对象/函数等引用类型） */
enum class DataKind : uint8_t {
  Number,   /* 数值 */
  Boolean,  /* 布尔 */
  String,   /* 字符串 */
  /* M3+ 扩展：Object / Array / Function / RegExp / Symbol / BigInt 等 */
};

/* Data：引用类型实际数据基类（多态，全部装箱）。
 * 继承 AllocatedObject：new/delete 经 core::Allocator 路由。
 * refcount：被多少 value 引用（create_value ++，value 析构 --）。 */
struct Data : AllocatedObject {
  explicit Data(DataKind k) : kind(k) {}
  virtual ~Data() = default;

  DataKind kind;
  int refcount = 0;
};

/* 数值数据 */
struct NumberData : Data {
  NumberData() : Data(DataKind::Number) {}
  double number = 0.0;
};

/* 布尔数据 */
struct BooleanData : Data {
  BooleanData() : Data(DataKind::Boolean) {}
  bool value = false;
};

/* 字符串数据（UTF-8 字节串） */
struct StringData : Data {
  StringData() : Data(DataKind::String) {}
  std::string str;
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_DATA_H
