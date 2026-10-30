/*
 * core 层：gc（value 生命周期管理，与内存管理解耦）。
 *
 * 遵循 docs/scope-redesign.md §4（修正版）：
 *   - GC 是 value 的唯一 owner：所有 value 经 gc.createValue 创建，加入
 *     全局表（all_values_）。
 *   - scope 不持有 value 所有权，只持引用清单（scope.values，退出时逐个
 *     scope_ref--）。
 *   - scope_ref-- 不 delete；delete 永远发生在 GC 扫描时（collect）。
 *     理由：value 若为对象，data 含引用其它 value 的字段，scope 退出时
 *     立即 delete 若仍有其它引用路径（闭包/跨 scope/栈借用）会悬垂；
 *     延迟到 GC 扫描统一处理对象字段安全。
 *   - 根集：scope_ref > 0 的 value 存活（当根跳过）。
 *   - data 生命周期归 value 析构：value 释放时 data refcount--，归零 delete。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#ifndef NEOJS_CORE_GC_H
#define NEOJS_CORE_GC_H

#include <unordered_set>

#include "neojs/core/allocator.h"
#include "neojs/core/data.h"
#include "neojs/core/value.h"

namespace neojs {
namespace core {

struct Scope;

/* Gc：value 生命周期管理器。
 * 所有 value 的唯一 owner（全局表）。scope 只持引用清单。 */
class Gc : AllocatedObject {
 public:
  Gc() = default;
  ~Gc();

  /* 创建 value：new Value(kind) + 加入全局表。返回原始 value*。
   * 调用方随后应把 value 挂到某 scope（ref）或交给引用者。 */
  Value *createValue(ValueKind kind);

  /* 创建带 data 的 value：new Value + new Data（refcount=1）+ 入全局表。 */
  Value *createNumber(double n);
  Value *createBoolean(bool b);
  Value *createString(const std::string &s);

  /* 特殊值工厂（无 data）。 */
  Value *createUndefined();
  Value *createNull();
  Value *createUninitialized();

  /* 扫描：遍历全局表，scope_ref == 0 的 value delete（+ 释放 data）。
   * 显式触发；scope 退出时也可调用（见 Context::pop_scope）。 */
  void collect();

  /* 扫描统计（调试/测试）：存活数 / 待回收数 / 已释放数。 */
  struct Stats {
    size_t total = 0;   /* 全局表中 value 总数 */
    size_t live = 0;    /* scope_ref > 0 存活 */
    size_t freed = 0;   /* 本次 collect 释放 */
  };
  Stats stats() const;

  /* 全局表只读访问（测试/调试）。 */
  size_t liveCount() const { return all_values_.size(); }

 private:
  /* 释放单个 value：data refcount-- 归零 delete，然后 delete value。 */
  void releaseValue(Value *v);

  std::unordered_set<Value *> all_values_; /* 全局表：所有 value（唯一 owner） */
};

}  // namespace core
}  // namespace neojs

#endif  // NEOJS_CORE_GC_H
