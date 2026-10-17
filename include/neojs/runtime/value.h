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
 */

#include <cstdint>
#include <string>

namespace neojs {

/* 与 C 头文件 JSKind 对应的内部 kind。保持数值一致。 */
enum class Kind : uint8_t {
  Undefined = 0,
  Null,
  Boolean,
  Number,
  String,
  Object,
  Exception,       /* #4：错误值 */
  InterruptYield,  /* #7：生成器 yield 断点 */
  InterruptAwait,  /* #7：async await 断点 */
};

class JSContext;

class JSValue {
 public:
  /* 构造：普通值 */
  JSValue();                                  /* undefined */
  explicit JSValue(Kind kind);                /* null / exception / interrupt 等无负载值 */
  JSValue(bool b);                            /* boolean */
  JSValue(double d);                          /* number */
  JSValue(int64_t i);                         /* number（整数） */
  JSValue(const std::string &s);              /* string */

  /* 访问器 */
  Kind kind() const { return kind_; }
  bool isNumber() const { return kind_ == Kind::Number; }
  bool isString() const { return kind_ == Kind::String; }
  bool isException() const { return kind_ == Kind::Exception; }

  /* 值转换（仅当 kind 匹配时语义正确） */
  bool toBoolean() const { return bool_; }
  double toNumber() const { return number_; }
  int64_t toInteger() const { return static_cast<int64_t>(number_); }
  const std::string &toString() const { return string_; }

  /* 错误值：设置/读取错误信息（kind == Exception 时有效） */
  void setErrorInfo(const std::string &info) { string_ = info; }
  const std::string &errorInfo() const { return string_; }

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
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_VALUE_H
