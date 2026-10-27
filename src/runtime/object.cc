#include "neojs/runtime/object.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/conversion.h"
#include "neojs/runtime/heap.h"
#include "neojs/runtime/value.h"

namespace neojs {

JSObject::JSObject(JSObject *prototype) : prototype_(prototype) {}

JSObject::JSObject(ObjectKind kind, size_t capacity) : kind_(kind) {
  if (capacity > 0) elements_.reserve(capacity);
}

/* 包装对象（E4-2）：kind 为 Number/String/Boolean/Symbol，data 为原始值。 */
JSObject::JSObject(ObjectKind kind, JSValue *data) : kind_(kind), data_(data) {}

/* -------------------------------------------------------------------------- */
/* 命名属性（properties 表）                                                   */
/* -------------------------------------------------------------------------- */

bool JSObject::hasOwnProperty(const std::string &name) const {
  return properties_.find(name) != properties_.end();
}

PropertyDescriptor *JSObject::getOwnProperty(const std::string &name) {
  auto it = properties_.find(name);
  return it == properties_.end() ? nullptr : &it->second;
}

const PropertyDescriptor *JSObject::getOwnProperty(const std::string &name) const {
  auto it = properties_.find(name);
  return it == properties_.end() ? nullptr : &it->second;
}

void JSObject::setOwnProperty(const std::string &name, const PropertyDescriptor &desc) {
  /* 记录字符串键创建顺序（§9.1.12 [[OwnPropertyKeys]]：字符串按创建序）。
   * 已存在则更新不改变顺序。 */
  if (properties_.find(name) == properties_.end()) {
    property_order_.push_back(name);
  }
  properties_[name] = desc;
}

bool JSObject::deleteOwnProperty(const std::string &name) {
  if (properties_.erase(name) == 0) return false;
  for (size_t i = 0; i < property_order_.size(); ++i) {
    if (property_order_[i] == name) {
      property_order_.erase(property_order_.begin() + i);
      break;
    }
  }
  return true;
}

/* -------------------------------------------------------------------------- */
/* Symbol 属性（M4.4d）：键为 Symbol 值指针，引用相等                           */
/* -------------------------------------------------------------------------- */

bool JSObject::hasOwnSymbolProperty(JSValue *sym) const {
  return symbol_properties_.find(sym) != symbol_properties_.end();
}

PropertyDescriptor *JSObject::getOwnSymbolProperty(JSValue *sym) {
  auto it = symbol_properties_.find(sym);
  return it == symbol_properties_.end() ? nullptr : &it->second;
}

void JSObject::setOwnSymbolProperty(JSValue *sym,
                                    const PropertyDescriptor &desc) {
  symbol_properties_[sym] = desc;
}

bool JSObject::deleteOwnSymbolProperty(JSValue *sym) {
  return symbol_properties_.erase(sym) > 0;
}

JSValue *JSObject::getSymbolProperty(JSValue *sym) {
  for (JSObject *o = this; o != nullptr; o = o->prototype_) {
    PropertyDescriptor *desc = o->getOwnSymbolProperty(sym);
    if (desc != nullptr) return desc->value;
  }
  return nullptr;
}

void JSObject::setSymbolProperty(JSValue *sym, JSValue *value) {
  PropertyDescriptor *desc = getOwnSymbolProperty(sym);
  if (desc != nullptr) {
    desc->value = value;
  } else {
    PropertyDescriptor d;
    d.value = value;
    setOwnSymbolProperty(sym, d);
  }
}

bool JSObject::hasSymbolProperty(JSValue *sym) const {
  for (const JSObject *o = this; o != nullptr; o = o->prototype_) {
    if (o->hasOwnSymbolProperty(sym)) return true;
  }
  return false;
}

bool JSObject::deleteSymbolProperty(JSValue *sym) {
  return deleteOwnSymbolProperty(sym);
}

/* -------------------------------------------------------------------------- */
/* 数组元素（elements 分离存储）                                               */
/* -------------------------------------------------------------------------- */

bool JSObject::hasElement(size_t index) const {
  return index < elements_.size() && elements_[index] != nullptr;
}

JSValue *JSObject::getElement(size_t index) const {
  if (index >= elements_.size()) return nullptr;
  return elements_[index]; /* 空洞返回 nullptr */
}

void JSObject::setElement(size_t index, JSValue *value) {
  if (index >= elements_.size()) elements_.resize(index + 1, nullptr); /* 自动扩容 */
  elements_[index] = value;
}

void JSObject::deleteElement(size_t index) {
  if (index < elements_.size()) elements_[index] = nullptr; /* 留空洞（稀疏数组） */
}

void JSObject::resizeElements(size_t length) {
  elements_.resize(length, nullptr); /* 加长空洞为 nullptr；缩短截断 */
}

/* -------------------------------------------------------------------------- */
/* 集合存储（M4.4a，[[MapData]]/[[SetData]]）                                   */
/* -------------------------------------------------------------------------- */

bool JSObject::collectionHas(JSValue *key) {
  for (const MapEntry &e : map_entries_) {
    if (sameValueZero(e.key, key)) return true;
  }
  return false;
}

JSValue *JSObject::collectionGet(JSValue *key) {
  for (const MapEntry &e : map_entries_) {
    if (sameValueZero(e.key, key)) return e.value;
  }
  return nullptr;
}

void JSObject::collectionSet(JSValue *key, JSValue *value) {
  for (MapEntry &e : map_entries_) {
    if (sameValueZero(e.key, key)) {
      e.value = value; /* 已存在：更新值 */
      return;
    }
  }
  map_entries_.push_back({key, value}); /* 追加新条目 */
}

bool JSObject::collectionDelete(JSValue *key) {
  for (size_t i = 0; i < map_entries_.size(); ++i) {
    if (sameValueZero(map_entries_[i].key, key)) {
      map_entries_.erase(map_entries_.begin() + static_cast<ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

void JSObject::collectionClear() { map_entries_.clear(); }

/* -------------------------------------------------------------------------- */
/* ordinary 属性语义（架构 §5.2，v1 简化：writable/configurable 不校验）        */
/* -------------------------------------------------------------------------- */

JSValue *JSObject::getProperty(const std::string &name) {
  for (JSObject *o = this; o != nullptr; o = o->prototype_) {
    PropertyDescriptor *desc = o->getOwnProperty(name);
    if (desc != nullptr) {
      /* v1 只支持数据属性；访问器属性（getter）M4 启用。 */
      return desc->value;
    }
  }
  return nullptr;
}

void JSObject::setProperty(const std::string &name, JSValue *value) {
  /* 简化：own 属性存在则更新，否则新增到本对象（不沿原型链校验 writable）。 */
  PropertyDescriptor *desc = getOwnProperty(name);
  if (desc != nullptr) {
    desc->value = value;
  } else {
    PropertyDescriptor d;
    d.value = value;
    setOwnProperty(name, d);
  }
}

bool JSObject::hasProperty(const std::string &name) const {
  for (const JSObject *o = this; o != nullptr; o = o->prototype_) {
    if (o->getOwnProperty(name) != nullptr) return true;
  }
  return false;
}

bool JSObject::deleteProperty(const std::string &name) {
  return deleteOwnProperty(name);
}

/* -------------------------------------------------------------------------- */
/* GC：引用边（docs/gc.md §4.1）                                               */
/* -------------------------------------------------------------------------- */

void JSObject::gcMarkRefs(Heap *heap) {
  /* 命名属性值 */
  for (auto &kv : properties_) {
    heap->markRoot(kv.second.value);
    heap->markRoot(kv.second.getter); /* 访问器预留字段 */
    heap->markRoot(kv.second.setter);
  }
  /* Symbol 属性（M4.4d）：键与值都是引用边 */
  for (auto &kv : symbol_properties_) {
    heap->markRoot(kv.first); /* Symbol 键值本身 */
    heap->markRoot(kv.second.value);
    heap->markRoot(kv.second.getter); /* 访问器预留字段 */
    heap->markRoot(kv.second.setter);
  }
  /* 数组元素 */
  for (JSValue *v : elements_) {
    heap->markRoot(v);
  }
  /* 集合条目（M4.4a：键与值都是引用边） */
  for (const MapEntry &e : map_entries_) {
    heap->markRoot(e.key);
    heap->markRoot(e.value);
  }
  /* 原型链 */
  heap->markRoot(prototype_);
  /* 包装对象内部槽（E4-2：被包装的原始值） */
  heap->markRoot(data_);
  /* 迭代器内部槽（M4.4f：被迭代对象） */
  heap->markRoot(iter_target_);
  /* Promise 决议值（M5.3a：[[PromiseResult]]，GC 引用边） */
  heap->markRoot(promise_result_);
  /* Generator 挂起现场（M5.5a-3：[[GeneratorContext]] AsyncFrame*，GC 引用边）。
   * AsyncFrame 自身 gcMarkRefs 标记其 fn/env/this/stack，此处只标记包装 JSValue。 */
  heap->markRoot(gen_frame_);
  /* AsyncGenerator 请求队列（M5.5c-1：[[AsyncGeneratorQueue]] 元素，GC 引用边）。
   * 元素为 AsyncGeneratorRequest 包装对象（{completion, capability}）。 */
  for (JSValue *q : async_queue_) {
    heap->markRoot(q);
  }
  /* 私有字段槽（M5.1d-c2：§6.1.7.5 私有字段值，GC 引用边） */
  for (auto &kv : private_slots_) {
    heap->markRoot(kv.second);
  }
}

/* -------------------------------------------------------------------------- */
/* 分配器（#5）                                                                */
/* -------------------------------------------------------------------------- */

void *JSObject::operator new(size_t size) { return ::operator new(size); }

void JSObject::operator delete(void *ptr) noexcept { ::operator delete(ptr); }

}  // namespace neojs
