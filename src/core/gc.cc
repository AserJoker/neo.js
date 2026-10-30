#include "neojs/core/gc.h"

#include <string>

namespace neojs {
namespace core {

Gc::~Gc() {
  /* 析构：释放全局表中所有剩余 value（无论 scope_ref 是否归零）。
   * ctx 销毁时 scope 树已先销毁（scope_ref--），这里统一回收。 */
  for (Value *v : all_values_) releaseValue(v);
  all_values_.clear();
}

/* ---------- value 工厂（全部入全局表） ---------- */

Value *Gc::createValue(ValueKind kind) {
  Value *v = new Value(kind);
  all_values_.insert(v);
  return v;
}

Value *Gc::createNumber(double n) {
  Value *v = createValue(ValueKind::Number);
  NumberData *d = new NumberData();
  d->refcount = 1;
  d->number = n;
  v->data = d;
  return v;
}

Value *Gc::createBoolean(bool b) {
  Value *v = createValue(ValueKind::Boolean);
  BooleanData *d = new BooleanData();
  d->refcount = 1;
  d->value = b;
  v->data = d;
  return v;
}

Value *Gc::createString(const std::string &s) {
  Value *v = createValue(ValueKind::String);
  StringData *d = new StringData();
  d->refcount = 1;
  d->str = s;
  v->data = d;
  return v;
}

Value *Gc::createUndefined() { return createValue(ValueKind::Undefined); }
Value *Gc::createNull() { return createValue(ValueKind::Null); }
Value *Gc::createUninitialized() {
  return createValue(ValueKind::Uninitialized);
}

/* ---------- 释放 ---------- */

void Gc::releaseValue(Value *v) {
  if (v == nullptr) return;
  /* data 生命周期归 value 析构：data refcount--，归零 delete。 */
  if (v->data != nullptr) {
    v->data->refcount--;
    if (v->data->refcount <= 0) delete v->data;
    v->data = nullptr;
  }
  delete v;
}

/* ---------- 扫描回收 ---------- */

void Gc::collect() {
  /* 遍历全局表，scope_ref == 0 的 value 回收。
   * 延迟 delete：scope 退出只 scope_ref--，这里统一处理，保证对象字段
   * （data 引用其它 value）在无其它引用路径时才释放，不悬垂。 */
  for (auto it = all_values_.begin(); it != all_values_.end();) {
    Value *v = *it;
    if (v->scope_ref <= 0) {
      releaseValue(v);
      it = all_values_.erase(it);
    } else {
      ++it;
    }
  }
}

Gc::Stats Gc::stats() const {
  Stats s;
  s.total = all_values_.size();
  for (Value *v : all_values_) {
    if (v->scope_ref > 0) ++s.live;
  }
  s.freed = s.total - s.live;
  return s;
}

}  // namespace core
}  // namespace neojs
