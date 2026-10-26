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

/* 对象类别（架构 §5.2：数组 elements 分离存储；E4-2 加包装对象）。
 * Number/String/Boolean/Symbol/BigInt 为原始值装箱（ToObject，§7.1.13），
 * 内部槽 data_ 持有被包装的原始值（GC 引用边）。M4.3e 加 BigInt 装箱。 */
enum class ObjectKind : uint8_t {
  Ordinary,
  Array,
  Number,   /* 包装 Number（[[NumberData]]） */
  String,   /* 包装 String（[[StringData]]） */
  Boolean,  /* 包装 Boolean（[[BooleanData]]） */
  Symbol,   /* 包装 Symbol（[[SymbolData]]） */
  BigInt,   /* 包装 BigInt（[[BigIntData]]，M4.3e） */
  Map,      /* Map（[[MapData]]，M4.4a） */
  Set,      /* Set（[[SetData]]，M4.4a） */
  ArrayIterator,  /* Array 迭代器（M4.4f，[[IteratedObject]]+[[ArrayIteratorNextIndex]]） */
  StringIterator, /* String 迭代器（M4.4f，[[IteratedString]]+[[StringIteratorNextIndex]]） */
  MapIterator,    /* Map 迭代器（M4.4f，[[IteratedMap]]+[[MapIteratorNextIndex]]） */
  SetIterator,    /* Set 迭代器（M4.4f，[[IteratedSet]]+[[SetIteratorNextIndex]]） */
  Promise,        /* Promise（M5.3a，§27.2：[[PromiseState]]+[[PromiseResult]]+reactions） */
  AsyncFrame,     /* async 挂起帧（M5.4，§27.7：VM 现场转移） */
  Generator,      /* Generator 对象（M5.5a-3，§27.5：[[GeneratorState]]+[[GeneratorContext]]） */
};

/* 集合条目（M4.4a）：Map 的键值对 / Set 的键。
 * key/value 均为 GC 引用边（gcMarkRefs 标记）。 */
struct MapEntry {
  JSValue *key = nullptr;
  JSValue *value = nullptr;
};

class JSObject : public GCHeader {
 public:
  /* 创建普通对象。prototype 可为 nullptr（对象无原型，仅当宿主显式指定）。 */
  JSObject(JSObject *prototype = nullptr);
  /* 创建数组对象。capacity 为元素存储初始容量。 */
  JSObject(ObjectKind kind, size_t capacity = 0);
  /* 创建包装对象（E4-2，§7.1.13 ToObject）：kind 为 Number/String/
   * Boolean/Symbol 之一，data 为被包装的原始值（内部指针）。 */
  JSObject(ObjectKind kind, JSValue *data);
  ~JSObject() override = default;

  ObjectKind kind() const { return kind_; }
  bool isArray() const { return kind_ == ObjectKind::Array; }
  bool isMap() const { return kind_ == ObjectKind::Map; }
  bool isSet() const { return kind_ == ObjectKind::Set; }
  bool isCollection() const { return isMap() || isSet(); }
  bool isWrapper() const { /* E4-2 装箱对象：内部持有原始值 */
    return kind_ == ObjectKind::Number || kind_ == ObjectKind::String ||
           kind_ == ObjectKind::Boolean || kind_ == ObjectKind::Symbol ||
           kind_ == ObjectKind::BigInt;
  }

  bool isIterator() const { /* M4.4f 内建迭代器（Array/String/Map/Set） */
    return kind_ == ObjectKind::ArrayIterator || kind_ == ObjectKind::StringIterator ||
           kind_ == ObjectKind::MapIterator || kind_ == ObjectKind::SetIterator;
  }

  /* 迭代器内部槽（M4.4f）：iter_target_ 为被迭代对象（GC 引用边），
   * iter_index_ 为当前推进索引。仅 isIterator() 时有效。 */
  JSValue *iterTarget() const { return iter_target_; }
  void setIterTarget(JSValue *v) { iter_target_ = v; }
  size_t iterIndex() const { return iter_index_; }
  void setIterIndex(size_t i) { iter_index_ = i; }

  /* Promise 内部槽（M5.3a，§27.2.2）[[PromiseState]]/[[PromiseResult]]：
   * 状态用整型（0 pending / 1 fulfilled / 2 rejected，见 promise.h
   * PromiseState 枚举）；result 为决议值（GC 引用边）。
   * 仅 isPromise() 时有效。 */
  bool isPromise() const { return kind_ == ObjectKind::Promise; }
  int promiseState() const { return promise_state_; }
  void setPromiseState(int s) { promise_state_ = s; }
  JSValue *promiseResult() const { return promise_result_; }
  void setPromiseResult(JSValue *v) { promise_result_ = v; }

  /* Generator 内部槽（M5.5a-3，§27.5）：[[GeneratorState]] 用整型
   * （0 suspended-start / 1 suspended-yield / 2 running / 3 completed），
   * [[GeneratorContext]] 为挂起现场（AsyncFrame*，GC 引用边）。
   * 仅 isGenerator() 时有效。 */
  bool isGenerator() const { return kind_ == ObjectKind::Generator; }
  int generatorState() const { return gen_state_; }
  void setGeneratorState(int s) { gen_state_ = s; }
  JSValue *generatorContext() const { return gen_frame_; }
  void setGeneratorContext(JSValue *v) { gen_frame_ = v; }

  /* 包装对象内部槽（E4-2）：[[NumberData]]/[[StringData]]/[[BooleanData]]/
   * [[SymbolData]]。仅 isWrapper() 时有效；GC 引用边。 */
  JSValue *wrapperData() const { return data_; }
  void setWrapperData(JSValue *v) { data_ = v; }

  /* 原型链（架构 §5.2） */
  JSObject *prototype() const { return prototype_; }
  void setPrototype(JSObject *proto) { prototype_ = proto; }

  /* [[Extensible]]（§9.4.1，M4.4g）：preventExtensions/freeze/seal 置 false。
   * 新属性添加、属性删除（configurable）受此约束；原型链不参与。 */
  bool extensible() const { return extensible_; }
  void setExtensible(bool e) { extensible_ = e; }

  /* 反向包装指针：持有本对象的 JSValue（对象工厂设置）。
   * 供原型链返回、GC 根集回查等需要 JSObject->JSValue 的场合。 */
  JSValue *holder() const { return holder_; }
  void setHolder(JSValue *v) { holder_ = v; }

  /* 命名属性（properties 表）。property_order_ 记录字符串键创建顺序
   * （§9.1.12 [[OwnPropertyKeys]]：整数索引升序 + 字符串按创建序），
   * Object.keys/values/entries 据此输出。 */
  bool hasOwnProperty(const std::string &name) const;
  PropertyDescriptor *getOwnProperty(const std::string &name);
  const PropertyDescriptor *getOwnProperty(const std::string &name) const;
  void setOwnProperty(const std::string &name, const PropertyDescriptor &desc);
  bool deleteOwnProperty(const std::string &name);
  const std::unordered_map<std::string, PropertyDescriptor> &properties() const {
    return properties_;
  }
  /* 字符串键创建顺序（§9.1.12）：供 Object.keys/values/entries 按序输出。 */
  const std::vector<std::string> &propertyOrder() const { return property_order_; }

  /* Symbol 属性（M4.4d，§7.3.22 等）：键为 Symbol 值指针，引用相等
   * （Symbol 每次创建唯一，SameValue 即引用相等）。独立表存储，
   * 与字符串属性互不干扰。键/值都是 GC 引用边。 */
  bool hasOwnSymbolProperty(JSValue *sym) const;
  PropertyDescriptor *getOwnSymbolProperty(JSValue *sym);
  void setOwnSymbolProperty(JSValue *sym, const PropertyDescriptor &desc);
  bool deleteOwnSymbolProperty(JSValue *sym);
  JSValue *getSymbolProperty(JSValue *sym);
  void setSymbolProperty(JSValue *sym, JSValue *value);
  bool hasSymbolProperty(JSValue *sym) const;
  bool deleteSymbolProperty(JSValue *sym);
  const std::unordered_map<JSValue *, PropertyDescriptor> &symbolProperties() const {
    return symbol_properties_;
  }

  /* 数组元素（elements 分离存储，架构 §5.2）。索引按 uint32_t 存储。 */
  size_t length() const { return elements_.size(); }
  bool hasElement(size_t index) const;
  JSValue *getElement(size_t index) const; /* 越界/空洞返回 nullptr */
  void setElement(size_t index, JSValue *value); /* 自动扩容 */
  void deleteElement(size_t index);
  void resizeElements(size_t length); /* 直接设置元素存储大小（length 属性） */
  const std::vector<JSValue *> &elements() const { return elements_; }

  /* 集合存储（M4.4a，[[MapData]]/[[SetData]]）。
   * 平铺条目 vector + SameValueZero 线性扫描（正确性优先，
   * 性能优化——值哈希分桶——留后续）。Set 的 value 存 key 自身。 */
  size_t collectionSize() const { return map_entries_.size(); }
  bool collectionHas(JSValue *key);
  JSValue *collectionGet(JSValue *key); /* Map：命中返回值，未命中 nullptr */
  void collectionSet(JSValue *key, JSValue *value); /* 存在则更新，否则追加 */
  bool collectionDelete(JSValue *key);
  void collectionClear();
  const std::vector<MapEntry> &collectionEntries() const { return map_entries_; }

  /* 私有字段槽（M5.1d-c2，§6.1.7.5 [[PrivateFieldValues]]）。
   * 独立表存储：键为字段全名（含 '#' 前缀，如 "#x"），普通属性读写/
   * 遍历永远看不到（真正不可见不可枚举）。值为 GC 引用边。
   * 接收者校验：hasPrivateSlot 为 false 即 TypeError（访问了未声明
   * 该私有字段的类实例）。 */
  bool hasPrivateSlot(const std::string &name) const {
    return private_slots_.find(name) != private_slots_.end();
  }
  JSValue *getPrivateSlot(const std::string &name) const {
    auto it = private_slots_.find(name);
    return it == private_slots_.end() ? nullptr : it->second;
  }
  void setPrivateSlot(const std::string &name, JSValue *value) {
    private_slots_[name] = value;
  }

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
  bool extensible_ = true;                               /* [[Extensible]]（M4.4g） */
  JSObject *prototype_ = nullptr;                            /* [[Prototype]] */
  std::unordered_map<std::string, PropertyDescriptor> properties_; /* 命名属性表 */
  std::vector<std::string> property_order_; /* 字符串键创建顺序（§9.1.12，M4.4g） */
  std::unordered_map<JSValue *, PropertyDescriptor> symbol_properties_; /* Symbol 属性表（M4.4d） */
  std::vector<JSValue *> elements_;                          /* 数组元素（连续索引） */
  std::vector<MapEntry> map_entries_;                        /* 集合条目（M4.4a，[[MapData]]/[[SetData]]） */
  std::unordered_map<std::string, JSValue *> private_slots_; /* 私有字段槽（M5.1d-c2，§6.1.7.5） */
  JSValue *holder_ = nullptr;                                /* 反向包装指针 */
  JSValue *data_ = nullptr;                                  /* 包装对象内部槽（E4-2） */
  JSValue *iter_target_ = nullptr;                           /* 迭代器内部槽（M4.4f） */
  size_t iter_index_ = 0;                                    /* 迭代器当前索引（M4.4f） */
  int promise_state_ = 0;                                    /* Promise 状态（M5.3a，0 pending/1 fulfilled/2 rejected） */
  JSValue *promise_result_ = nullptr;                        /* Promise 决议值（M5.3a，[[PromiseResult]]，GC 引用边） */
  int gen_state_ = 0;                                        /* Generator 状态（M5.5a-3，0 start/1 yield/2 running/3 completed） */
  JSValue *gen_frame_ = nullptr;                             /* Generator 挂起现场（M5.5a-3，AsyncFrame*，GC 引用边） */
};

}  // namespace neojs

#endif  // NEOJS_RUNTIME_OBJECT_H
