#include "neojs/runtime/value.h"

#include <cstdlib>
#include <new>

#include "neojs/runtime/context.h"

namespace neojs {

namespace {

/* 全局默认分配器（当宿主未注入自定义分配器时使用，#5）。 */
struct DefaultAllocator {
  static void *alloc(size_t size) { return std::malloc(size); }
  static void freeFn(void *ptr) { std::free(ptr); }
};

}  // namespace

/* -------------------------------------------------------------------------- */
/* JSValue 构造                                                                */
/* -------------------------------------------------------------------------- */

JSValue::JSValue() : kind_(Kind::Undefined), bool_(false), number_(0.0) {}

JSValue::JSValue(Kind kind) : kind_(kind), bool_(false), number_(0.0) {}

JSValue::JSValue(bool b) : kind_(Kind::Boolean), bool_(b), number_(0.0) {}

JSValue::JSValue(double d) : kind_(Kind::Number), bool_(false), number_(d) {}

JSValue::JSValue(int64_t i) : kind_(Kind::Number), bool_(false), number_(static_cast<double>(i)) {}

JSValue::JSValue(const std::string &s)
    : kind_(Kind::String), bool_(false), number_(0.0), string_(s) {}

/* -------------------------------------------------------------------------- */
/* 分配器（#5：内部重写 new/delete 走统一分配器）                               */
/* -------------------------------------------------------------------------- */

/*
 * 说明：JSValue 由 ctx 统一管理生命周期（registerValue / freeValue），
 * 这里重写 operator new/delete 仅为提供统一分配入口的范式。
 * 默认使用 ::operator new / ::operator delete（可被全局替换）。
 */
void *JSValue::operator new(size_t size) { return ::operator new(size); }

void JSValue::operator delete(void *ptr) noexcept { ::operator delete(ptr); }

}  // namespace neojs
