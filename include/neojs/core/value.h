/*
 * core 层：value（值语义实体）。
 *
 * 遵循 docs/scope-redesign.md §1.1/§4：
 *   - value 是值语义实体：return/传参/赋值都产生新 value（拷贝语义）。
 *   - value 持 data*（引用类型实际数据，data refcount 管理复用）。
 *   - scope_ref：被多少 scope 持有（归零 delete）。作用域退出遍历 values
 *     逐个 scope_ref--。
 *   - 特殊值：UNDEFINED / NULL / UNINITIALIZED（TDZ 哨兵）/ EXCEPTION /
 *     INTERRUPT（控制流中断载体）/ COROUTINE（挂起的协程现场）。
 *   - generator/async 共用同一协程现场：挂起原因（yield/await）在
 *     InterruptData.kind，调度方式（next vs promise）由持有者决定。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_CORE_VALUE_H
#define NEOJS_CORE_VALUE_H

#include <cstdint>

#include "neojs/core/allocator.h"
#include "neojs/core/data.h"

namespace neojs {
namespace core {

/* value 类型 */
enum class ValueKind : uint8_t {
  Undefined,     /* undefined */
  Null,          /* null */
  Boolean,       /* 布尔（data=BooleanData） */
  Number,        /* 数值（data=NumberData） */
  String,        /* 字符串（data=StringData） */
  /* M3+ 扩展：BigInt / Symbol / Object / Function / Array / RegExp */

  /* 特殊值（无 data 或 data 为内部载体） */
  Uninitialized, /* TDZ 哨兵：作用域进入时 let/const 初值，读它抛 ReferenceError */
  Exception,     /* 错误值（interrupt 的 data，见 interrupt） */
  Interrupt,     /* 控制流中断载体：data=InterruptData{kind,value,label} */
  Coroutine,     /* 挂起的协程现场：data=CoroutineData（打包的 vm）§9。
                  * generator/async 共用：挂起原因在 InterruptData.kind，
                  * 调度方式（next vs promise）由持有者决定。 */
};

/* Value：值语义实体（堆分配，继承 AllocatedObject）。
 * data*：引用数据（refcount 共享）或 nullptr（undef/null/uninit 无 data）。
 * scope_ref：被多少 scope 持有（加入 scope.values 时 ++，scope 销毁时 --，
 *   归零 delete）。栈中存的全部是借用值，不计数。 */
struct Value : AllocatedObject {
  explicit Value(ValueKind k) : kind(k) {}

  ValueKind kind = ValueKind::Undefined;
  Data *data = nullptr;   /* 引用数据或 nullptr */
  int scope_ref = 0;      /* 被多少 scope 持有（归零 delete） */
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_VALUE_H
