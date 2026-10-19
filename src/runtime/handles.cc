#include "neojs/runtime/handles.h"

#include "neojs/runtime/value.h"

namespace neojs {

HandleTable::HandleTable() {
  /* 句柄 0 预留为无效句柄（#14）。 */
  slots_.push_back(nullptr);
}

HandleTable::~HandleTable() = default;

uint32_t HandleTable::alloc(JSValue *value) {
  if (value == nullptr) return 0; /* 无效句柄 */
  uint32_t h = nextHandle();
  slots_[h] = value;
  return h;
}

void HandleTable::free(uint32_t handle) {
  if (handle == 0 || handle >= slots_.size()) return; /* 无效/越界 */
  if (slots_[handle] == nullptr) return;               /* 已是空槽 */
  slots_[handle] = nullptr;
  free_.push_back(handle); /* 槽位复用 */
}

JSValue *HandleTable::get(uint32_t handle) const {
  if (handle == 0 || handle >= slots_.size()) return nullptr;
  return slots_[handle];
}

uint32_t HandleTable::find(JSValue *value) const {
  if (value == nullptr) return 0;
  for (uint32_t i = 1; i < slots_.size(); ++i) {
    if (slots_[i] == value) return i;
  }
  return 0;
}

std::vector<JSValue *> HandleTable::roots() const {
  std::vector<JSValue *> out;
  for (JSValue *v : slots_) {
    if (v != nullptr) out.push_back(v);
  }
  return out;
}

uint32_t HandleTable::nextHandle() {
  if (!free_.empty()) {
    uint32_t h = free_.back();
    free_.pop_back();
    return h;
  }
  slots_.push_back(nullptr);
  return static_cast<uint32_t>(slots_.size() - 1);
}

}  // namespace neojs
