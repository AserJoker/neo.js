#ifndef NEOJS_RUNTIME_OBJECT_H
#define NEOJS_RUNTIME_OBJECT_H

/*
 * 内部 C++ 实现：JSObject（对象模型）。
 *
 * 遵循 docs/architecture.md §5.2 对象模型：
 *   - v1 阶段对象属性直接放 std::unordered_map（正确性优先）；预留 Shape 抽象。
 *   - 数组：elements 分离存储（连续索引）+ properties（命名属性），
 *     length 由元素存储推导。
 *   - 原型链：[[Prototype]] 指针；属性查找遵循 ordinary [[Get]] 沿原型链。
 *   - 访问器属性：getter/setter（Object.defineProperty）与数据属性统一在
 *     PropertyDescriptor 中建模（getter/setter 字段预留，M4 内置启用）。
 *
 * GC（docs/gc.md §3.2/§4.1）：JSObject 继承 GCHeader，属性值/数组元素/原型
 * 都是引用边，经 gcMarkRefs 交给 Heap 递归标记；循环引用可回收。
 *
 * 注意：此头文件是引擎内部实现，不对外暴露。
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "neojs/runtime/allocator.h"
#include "neojs/runtime/gc_header.h"

namespace neojs {

class Heap;
class JSValue;

/* 属性描述符（架构 §5.2）：数据属性与访问器属性统一建模。 */
struct PropertyDescriptor {
  /* 属性类别：v1 只实现数据属性；访问器属性字段预留（M4 Object.defineProperty 启用）。 */
  enum class Type : uint8_t { Data, Accessor };

  Type type = Type::Data;
  bool writable = true;
  bool enumerable = true;
  bool configurable = true;

  /* Data 属性载荷 */
  JSValue *value = nullptr;

  /* Accessor 属性载荷（预留：getter/setter 函数对象；v1 未启用） */
  JSValue *getter = nullptr;
  JSValue *setter = nullptr;
};

/* 对象类别（架构 §5.2：数组 elements 分离存储）。 */
enum class ObjectKind : uint8_t { Ordinary, Array };

class JSObject : public GCHeader {
 public:
  /* 创建普通对象。prototype 可为 nullptr（对象无原型，仅当宿主显式指定）。 */
  JSObject(JSObject *prototype = nullptr);
  /* 创建数组对象。capacity 为元素存储初始容量。 */
  JSObject(ObjectKind kind, size_t capacity = 0);
  ~JSObject() override = default;

  ObjectKind kind() const { return kind_; }
  bool isArray() const { return kind_ == ObjectKind::Array; }

  /* 原型链（架构 §5.2） */
  JSObject *prototype() const { return prototype_; }
  void setPrototype(JSObject *proto) { prototype_ = proto; }

  /* 反向包装指针：持有本对象的 JSValue（对象工厂设置）。
   * 供原型链返回、GC 根集回查等需要 JSObject->JSValue 的场合。 */
  JSValue *holder() const { return holder_; }
  void setHolder(JSValue *v) { holder_ = v; }

  /* 命名属性（properties 表） */
  bool hasOwnProperty(const std::string &name) const;
  PropertyDescriptor *getOwnProperty(const std::string &name);
  const PropertyDescriptor *getOwnProperty(const std::string &name) const;
  void setOwnProperty(const std::string &name, const PropertyDescriptor &desc);
  bool deleteOwnProperty(const std::string &name);
  const std::unordered_map<std::string, PropertyDescriptor> &properties() const {
    return properties_;
  }

  /* 数组元素（elements 分离存储，架构 §5.2）。索引按 uint32_t 存储。 */
  size_t length() const { return elements_.size(); }
  bool hasElement(size_t index) const;
  JSValue *getElement(size_t index) const; /* 越界/空洞返回 nullptr */
  void setElement(size_t index, JSValue *value); /* 自动扩容 */
  void deleteElement(size_t index);
  void resizeElements(size_t length); /* 直接设置元素存储大小（length 属性） */
  const std::vector<JSValue *> &elements() const { return elements_; }

  /* GC（docs/gc.md §4.1）：属性值 + 数组元素 + 原型都是引用边。 */

  /* ordinary 属性语义（架构 §5.2，v1 简化：writable/configurable 不校验）：
   * getProperty    沿原型链查找，返回持属性的值指针（未找到 nullptr）。
   * setProperty    赋值：own 属性存在则更新，否则新增到本对象。
   * hasProperty    沿原型链是否存在。
   * deleteProperty 删除 own 属性（返回是否删除）。 */
  JSValue *getProperty(const std::string &name);
  void setProperty(const std::string &name, JSValue *value);
  bool hasProperty(const std::string &name) const;
  bool deleteProperty(const std::string &name);
  void gcMarkRefs(Heap *heap) override;

  /* 分配器支持（#5：内部重写 new/delete 走统一分配器） */
  static void *operator new(size_t size);
  static void operator delete(void *ptr) noexcept;
  static void *operator new(size_t, void *ptr) { return ptr; }

 private:
  ObjectKind kind_ = ObjectKind::Ordinary;
  JSObject *prototype_ = nullptr;                            /* [[Prototype]] */
  std::unordered_map<std::string, PropertyDescriptor> properties_; /* 命名属性表 */
  std::vector<JSValue *> elements_;                          /* 数组元素（连续索引） */
  JSValue *holder_ = nullptr;                                /* 反向包装指针 */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_OBJECT_H
