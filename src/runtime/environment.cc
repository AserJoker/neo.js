#include "neojs/runtime/environment.h"

#include "neojs/runtime/heap.h"
#include "neojs/runtime/value.h"

namespace neojs {

void JSEnvironment::gcMarkRefs(Heap *heap) {
  for (const auto &kv : bindings_) {
    if (kv.second != nullptr) heap->markRoot(kv.second);
  }
  if (parent_ != nullptr) heap->markRoot(parent_);
}

}  // namespace neojs
