#ifndef NEOJS_RUNTIME_HANDLES_H
#define NEOJS_RUNTIME_HANDLES_H

/*
 * 内部 C++ 实现：HandleTable（宿主句柄表）。
 *
 * 遵循 docs/gc.md（GC 引擎设计）：
 *   G2  宿主经句柄（Handle）访问值；C API 返回/接收的是句柄（表索引），
 *       非裸指针；GC 回收不影响宿主已持有的句柄（QuickJS/V8 经典做法）。
 *   G4  非移动式：对象地址不变，句柄表稳定性由表索引保证。
 *
 * 句柄 = 表索引（uint32_t）。句柄 0 预留为无效句柄（#14：接口返回的句柄
 * 永不返回 0，错误也返回指向 Exception 对象的句柄）。
 *
 * 注意：句柄表内部槽位 vector 是引擎元数据（非 JS 值），用默认分配；
 * 值对象本身的分配/回收由 Heap 经 JSAllocator 统一路由（#5/G1）。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstdint>
#include <vector>

namespace neojs {

class JSValue;

class HandleTable {
 public:
  HandleTable();
  ~HandleTable();

  /* 分配一个新句柄并指向 value；返回句柄（表索引，非 0）。 */
  uint32_t alloc(JSValue *value);

  /* 释放句柄槽（对象交给 GC 判断生死，不立即释放）。空槽可复用。 */
  void free(uint32_t handle);

  /* 句柄 -> 对象；无效句柄 / 空槽返回 nullptr。 */
  JSValue *get(uint32_t handle) const;

  /* 对象 -> 句柄；未持有则返回 0。 */
  uint32_t find(JSValue *value) const;

  /* 所有非空槽（GC 根集之一：宿主持有的值不可回收）。 */
  std::vector<JSValue *> roots() const;

  /* 当前已分配句柄数（调试/测试观测） */
  size_t size() const { return slots_.size() - free_.size(); }

 private:
  uint32_t nextHandle();  /* 新句柄：优先复用 free_，否则追加新槽 */

  std::vector<JSValue *> slots_;           /* 句柄槽（索引 = 句柄；0 预留无效） */
  std::vector<uint32_t> free_;             /* 空闲槽位栈（复用） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_HANDLES_H
