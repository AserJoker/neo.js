#include "neojs/runtime/context.h"

namespace neojs {

JSContext::JSContext(JSAllocator *allocator)
    : allocator_(allocator),
      /* 初始值作用域由 ctx 经分配器创建（归属 ctx，释放 ctx 时一并回收） */
      current_scope_(allocator_->create<JSScope>(allocator)) {}

JSContext::~JSContext() {
  /* current_scope 析构时统一回收所有归属值（#5：作用域管理值生命周期）。
   * 注意：ctx 不拥有分配器，这里只释放值 + 作用域，不释放 allocator。 */
  allocator_->destroy(current_scope_);
}

void JSContext::deallocateSelf() { allocator_->deallocate(this); }

}  // namespace neojs
