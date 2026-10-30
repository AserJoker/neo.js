#include "neojs/core/context.h"

#include <utility>

namespace neojs {
namespace core {

/* ---------- 构造/析构 ---------- */

Context::Context() : root_scope(new Scope()), current_scope(root_scope) {}

Context::~Context() {
  /* rootScope 常驻：随 ctx 销毁而销毁。
   * 先销毁 scope 树（values 逐个 scope_ref--，不 delete value），
   * 再让 gc_ 析构回收全局表中所有剩余 value。 */
  destroy_scope(root_scope);
  root_scope = nullptr;
  current_scope = nullptr;
}

/* ---------- value 工厂（创建经 gc，挂 current_scope，scope_ref++） ---------- */

Value *Context::create_value(ValueKind kind) {
  Value *v = gc_.createValue(kind);
  return attach(current_scope, v);
}

Value *Context::create_number(double n) {
  Value *v = gc_.createNumber(n);
  return attach(current_scope, v);
}

Value *Context::create_boolean(bool b) {
  Value *v = gc_.createBoolean(b);
  return attach(current_scope, v);
}

Value *Context::create_string(const std::string &s) {
  Value *v = gc_.createString(s);
  return attach(current_scope, v);
}

Value *Context::create_undefined() { return create_value(ValueKind::Undefined); }
Value *Context::create_null() { return create_value(ValueKind::Null); }
Value *Context::create_uninitialized() {
  return create_value(ValueKind::Uninitialized);
}

/* ---------- scope 管理 ---------- */

Scope *Context::push_scope() {
  Scope *s = new Scope();
  s->parent = current_scope;
  if (current_scope != nullptr) current_scope->children.push_back(s);
  current_scope = s;
  return s;
}

void Context::pop_scope() {
  Scope *s = current_scope;
  if (s == nullptr || s == root_scope) return;
  current_scope = s->parent;
  /* 只销毁 scope（scope_ref--），不立即回收 value。
   * delete 永远由 Gc::collect 扫描时统一处理（§4 修正：对象字段安全）；
   * 触发时机由调用方/engine 决定（函数返回、模块边界、显式 collect）。 */
  destroy_scope(s);
}

void Context::destroy_scope(Scope *scope) {
  if (scope == nullptr) return;
  /* 1. 从 parent 摘除自己 */
  if (scope->parent != nullptr) {
    auto &ch = scope->parent->children;
    for (auto it = ch.begin(); it != ch.end(); ++it) {
      if (*it == scope) {
        ch.erase(it);
        break;
      }
    }
  }
  /* 2. 递归销毁所有 children */
  for (Scope *child : scope->children) {
    child->parent = nullptr; /* 防止重复摘除 */
    destroy_scope(child);
  }
  scope->children.clear();
  /* 3. 遍历 values 逐个 scope_ref--（只减计数，不 delete）。
   *    delete 由 Gc::collect 延迟执行（§4 修正：对象字段安全）。 */
  for (Value *v : scope->values) {
    if (v == nullptr) continue;
    v->scope_ref--;
  }
  scope->values.clear();
  delete scope;
}

/* ---------- 绑定 ---------- */

Value *Context::attach(Scope *scope, Value *value) {
  if (scope == nullptr || value == nullptr) return value;
  value->scope_ref++;
  scope->values.push_back(value);
  return value;
}

void Context::define(const std::string &name, Value *value) {
  if (current_scope == nullptr) return;
  /* 具名绑定指向 value*（引用清单）；同一 value 可被多 scope 引用。
   * 已存在则覆盖指针（不改变引用计数）。 */
  current_scope->named_values[name] = value;
}

Value *Context::load(const std::string &name) {
  return load_from(current_scope, name);
}

Value *Context::load_from(Scope *start, const std::string &name) {
  for (Scope *s = start; s != nullptr; s = s->parent) {
    auto it = s->named_values.find(name);
    if (it != s->named_values.end()) return it->second;
  }
  return nullptr;
}

bool Context::store(const std::string &name, Value *new_value) {
  Value *target = load(name);
  if (target == nullptr) return false;
  /* store 改 data 内容，不顶替变量：target->data = clone(new_value->data)。 */
  if (new_value->data != nullptr) {
    Data *cloned = clone(new_value->data);
    /* 释放旧 data（归零 delete），换上新 data */
    if (target->data != nullptr) {
      target->data->refcount--;
      if (target->data->refcount <= 0) delete target->data;
    }
    target->data = cloned;
    /* target 的 kind 也要同步（data 类型变了） */
    target->kind = new_value->kind;
  } else {
    /* 无 data（undef/null/uninit）：target 指向空 */
    if (target->data != nullptr) {
      target->data->refcount--;
      if (target->data->refcount <= 0) delete target->data;
    }
    target->data = nullptr;
    target->kind = new_value->kind;
  }
  return true;
}

/* clone：按 data 类型分派（§3.5）。
 * M1 值类型（Number/Boolean/String）走深拷贝（new 新 data）；后续 Object 等
 * 引用类型改走 refcount 共享。 */
Data *Context::clone(Data *data) {
  if (data == nullptr) return nullptr;
  switch (data->kind) {
    case DataKind::Number: {
      NumberData *d = new NumberData();
      d->refcount = 1;
      d->number = static_cast<NumberData *>(data)->number;
      return d;
    }
    case DataKind::Boolean: {
      BooleanData *d = new BooleanData();
      d->refcount = 1;
      d->value = static_cast<BooleanData *>(data)->value;
      return d;
    }
    case DataKind::String: {
      StringData *d = new StringData();
      d->refcount = 1;
      d->str = static_cast<StringData *>(data)->str;
      return d;
    }
  }
  return nullptr; /* unreachable */
}

}  // namespace core
}  // namespace neojs
