#include "neojs/core/allocator.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace neojs {
namespace core {

/* oomAbort：alloc 失败由分配器自己终止（不返回 nullptr，#14）。 */
[[noreturn]] void Allocator::oomAbort() {
  std::fputs("neo.js: out of memory\n", stderr);
  std::abort();
}

/* 对齐分配（C++17 over-aligned 类型）。 */
void *AllocatedObject::operator new(std::size_t size, std::align_val_t align) {
  /* 简化：std::malloc 满足常规对齐；over-aligned 用 aligned_alloc。
   * 平台差异：MSVC 用 _aligned_malloc。 */
#if defined(_MSC_VER)
  void *ptr = _aligned_malloc(size, static_cast<std::size_t>(align));
#else
  void *ptr = std::aligned_alloc(static_cast<std::size_t>(align), size);
#endif
  if (ptr == nullptr) Allocator::oomAbort();
  return ptr;
}

void AllocatedObject::operator delete(void *ptr, std::align_val_t) noexcept {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

}  // namespace core
}  // namespace neojs
