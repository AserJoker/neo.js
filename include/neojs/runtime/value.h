#ifndef NEOJS_RUNTIME_VALUE_H
#define NEOJS_RUNTIME_VALUE_H

/*
 * 内部 C++ 实现：JSValue。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露（对外只暴露 neojs.h 的 C 接口）。
 * 遵循 docs/coding-standards.md：
 *   #3 不向 C ABI 边界抛出 C++ 异常；
 *   #4 JSValue 为一等公民，kind 区分普通值与错误/中断；
 *   #5 内部重写 new/delete 走统一分配器。
 *
 * 遵循 docs/gc.md（GC 引擎设计）：
 *   G1  JSValue 由 Heap 统一分配与回收（对象头内嵌 GC 标记位）。
 *   M2 阶段 JSValue 是叶子值（无引用边）；对象模型（M3）再加 refs_ 供 mark 遍历。
 */

#include <cstdint>
#include <string>

#include "neojs/runtime/gc_header.h"

namespace neojs {

/* 与 C 头文件 JSKind 对应的内部 kind。保持数值一致（按规范八种类型顺序）。 */
enum class Kind : uint8_t {
  Undefined = 0,
  Null,
  Boolean,
  Number,
  String,
  Symbol,
  Object,
  Exception,       /* #4：错误值 */
  InterruptYield,  /* #7：生成器 yield 断点 */
  InterruptAwait,  /* #7：async await 断点 */
};

class JSObject;
class JSContext;
class Heap;

class JSValue : public GCHeader {
 public:
  /* 构造：普通值 */
  JSValue();                                  /* undefined */
  explicit JSValue(Kind kind);                /* null / exception / interrupt 等无负载值 */
  JSValue(bool b);                            /* boolean */
  JSValue(double d);                          /* number */
  JSValue(int64_t i);                         /* number（整数） */
  JSValue(const std::string &s);              /* string */
  JSValue(const char *s, size_t len);         /* string（长度限定） */
  ~JSValue() override = default;

  /* 访问器 */
  Kind kind() const { return kind_; }
  bool isNumber() const { return kind_ == Kind::Number; }
  bool isString() const { return kind_ == Kind::String; }
  bool isSymbol() const { return kind_ == Kind::Symbol; }
  bool isObject() const { return kind_ == Kind::Object; }
  bool isException() const { return kind_ == Kind::Exception; }

  /* 对象值：仅当 isObject() 时有效。setObject 由对象工厂调用（kind_ 同步置 Object）。 */
  JSObject *object() const { return object_; }
  void setObject(JSObject *obj);

  /* 值转换（仅当 kind 匹配时语义正确） */
  bool toBoolean() const { return bool_; }
  double toNumber() const { return number_; }
  int64_t toInteger() const { return static_cast<int64_t>(number_); }
  const std::string &toString() const { return string_; }

  /* Symbol 描述符（description）：kind_ == Symbol 时有效；无描述为空串。 */
  const std::string &symbolDescription() const { return string_; }
  void setSymbolDescription(const std::string &desc) { string_ = desc; }

  /* 错误值：设置/读取错误信息（kind == Exception 时有效） */
  void setErrorInfo(const std::string &info) { string_ = info; }
  const std::string &errorInfo() const { return string_; }

  /* GC（docs/gc.md §3.2/§4.1）：引用边交给 Heap 递归标记（对象载荷）。 */
  void gcMarkRefs(Heap *heap) override;

  /* 分配器支持（#5：内部重写 new/delete 走统一分配器） */
  static void *operator new(size_t size);
  static void operator delete(void *ptr) noexcept;

  /* placement new：类自定义 operator new 后会隐藏全局 placement 版本，需显式提供 */
  static void *operator new(size_t, void *ptr) { return ptr; }

 private:
  Kind kind_;
  bool bool_;
  double number_;
  std::string string_;
  JSObject *object_ = nullptr; /* Object 载荷（kind_ == Object 时有效） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_VALUE_H
