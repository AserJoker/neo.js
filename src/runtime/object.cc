#include "neojs/runtime/object.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include "neojs/runtime/allocator.h"
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
  properties_[name] = desc;
}

bool JSObject::deleteOwnProperty(const std::string &name) {
  return properties_.erase(name) > 0;
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
  /* 数组元素 */
  for (JSValue *v : elements_) {
    heap->markRoot(v);
  }
  /* 原型链 */
  heap->markRoot(prototype_);
  /* 包装对象内部槽（E4-2：被包装的原始值） */
  heap->markRoot(data_);
}

/* -------------------------------------------------------------------------- */
/* 分配器（#5）                                                                */
/* -------------------------------------------------------------------------- */

void *JSObject::operator new(size_t size) { return ::operator new(size); }

void JSObject::operator delete(void *ptr) noexcept { ::operator delete(ptr); }

}  // namespace neojs
