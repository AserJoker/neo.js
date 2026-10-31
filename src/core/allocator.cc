#include "neojs/core/allocator.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h> /* _msize / _aligned_msize */
#endif

namespace neojs {
namespace core {

std::atomic<size_t> Allocator::allocated_{0};
std::atomic<size_t> Allocator::limit_{static_cast<size_t>(1) << 30}; /* 1 GiB */

/* oomAbort：alloc 失败或超上限由分配器自己终止（不返回 nullptr，#14）。
 * 打印当前活跃分配量便于诊断（分配失控定位）。 */
[[noreturn]] void Allocator::oomAbort() {
  std::fprintf(stderr,
               "neo.js: out of memory (limit=%llu bytes, allocated=%llu bytes)\n",
               static_cast<unsigned long long>(limit()),
               static_cast<unsigned long long>(allocated()));
  std::abort();
}

/* 尝试累加 size：未超上限返回 true（计数已更新），超限返回 false。 */
bool Allocator::reserve(size_t size) {
  size_t cur = allocated_.load(std::memory_order_relaxed);
  size_t nxt;
  do {
    size_t lim = limit_.load(std::memory_order_relaxed);
    /* 防下溢：cur 已超限或本次会超限 → 失败（不更新计数）。 */
    if (cur > lim || size > lim - cur) {
      return false;
    }
    nxt = cur + size;
  } while (!allocated_.compare_exchange_weak(cur, nxt,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed));
  return true;
}

/* free 时按块实际大小递减计数。
 * 平台相关：MSVC 用 _msize；其他平台无法取实际大小 → 计数高估
 * （只可能提前触发上限，不泄漏内存，可接受）。 */
void Allocator::release(void *ptr) {
#if defined(_MSC_VER)
  size_t sz = _msize(ptr);
  if (sz == 0) return;
  size_t cur = allocated_.load(std::memory_order_relaxed);
  size_t nxt = (cur > sz) ? cur - sz : 0;
  while (!allocated_.compare_exchange_weak(cur, nxt,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) {
    nxt = (cur > sz) ? cur - sz : 0;
  }
#else
  (void)ptr;
  /* 无法取大小：不减计数（高估安全方向） */
#endif
}

/* 对齐分配（C++17 over-aligned 类型）。 */
void *Allocator::alloc_aligned(size_t size, std::align_val_t align) {
  if (!reserve(size)) oomAbort();
#if defined(_MSC_VER)
  void *ptr = _aligned_malloc(size, static_cast<std::size_t>(align));
#else
  void *ptr = std::aligned_alloc(static_cast<std::size_t>(align), size);
#endif
  if (ptr == nullptr) oomAbort();
  return ptr;
}

/* 对齐释放：按块实际大小递减计数 + 平台 aligned 释放。 */
void Allocator::free_aligned(void *ptr, std::align_val_t align) {
  if (ptr == nullptr) return;
#if defined(_MSC_VER)
  size_t sz = _aligned_msize(ptr, static_cast<std::size_t>(align), 0);
  if (sz > 0) {
    size_t cur = allocated_.load(std::memory_order_relaxed);
    size_t nxt = (cur > sz) ? cur - sz : 0;
    while (!allocated_.compare_exchange_weak(
        cur, nxt, std::memory_order_relaxed, std::memory_order_relaxed)) {
      nxt = (cur > sz) ? cur - sz : 0;
    }
  }
  _aligned_free(ptr);
#else
  release(ptr); /* 非 MSVC：无法取实际大小，走普通 release（高估安全方向） */
  std::free(ptr);
#endif
}

}  // namespace core
}  // namespace neojs
