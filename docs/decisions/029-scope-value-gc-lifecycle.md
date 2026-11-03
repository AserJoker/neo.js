# ADR-029: Scope-Value-GC 生命周期管理（M2 第一阶段，扁平 Value）

- 状态: 已接受 (2026-11-03)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-008（Allocator/GC 分层）、ADR-010（接口-实现-工厂）、ADR-012（Scope 树 + scope_ref）、
          ADR-015（Value meta+data 与引用计数分层）、ADR-016（Type 体系）、ADR-028（infra）

## 背景

进入 M2 阶段，需要把 ADR-008/012/015 既定的内存架构落地为可运行的 Scope-Value-GC 生命周期
管线。本阶段**不引入复杂对象**（Object/Array/Function 等带子 value 的容器类型留到后续），
Value 是**扁平的**——meta=Type* + data=Data*，data 不持有子 value，无对象图嵌套。

目标：先把"创建→跟踪→标记→回收"的生命周期管线跑通，验证 GC 与 Allocator 配额联动、
scope_ref 根标记、data 引用计数分层销毁等核心机制。复杂对象图标记（沿 data 子 value 递归）
的框架在本阶段就位，但实际遍历为 noop（data 扁平无子 value）。

## 决策

### 1. 阶段范围：扁平 Value

本阶段 Value 的 data 是**扁平基础类型数据**（undefined/null/boolean/number/string 等），
data **不持有子 value**。因此：
- GC 标记阶段沿 data 引用链遍历的框架就位，但本阶段遍历为 noop。
- 无对象图循环问题（环在复杂对象阶段才出现）。
- data.ref 引用计数仍保留（为后续复杂对象一致），但本阶段一个 data 通常只被一个 Value 指向。

### 2. Value 内存模型（落实 ADR-015）

```cpp
// src/public/core/value.h
namespace neo::core {

class Value {
    Type* type_;   // meta：类型描述（单例/共享，GC 不管理 Type 生命周期）
    Data* data_;   // data：数据负载（引用计数管理）
    int   scope_ref_ = 0;  // 被几个 scope 跟踪（GC 根判定，ADR-012）
public:
    // 构造时 data->ref++（接管引用计数），析构时 data->ref--（归 0 销毁 data）
    Value(Type* type, Data* data);
    ~Value();

    Type* type() const { return type_; }
    Data* data() const { return data_; }
    int   scope_ref() const { return scope_ref_; }

    void  scope_ref_inc() { ++scope_ref_; }
    void  scope_ref_dec() { --scope_ref_; }
};

}  // namespace neo::core
```

- **Value 构造**：`data->ref++`，建立"Value 指向 data"的引用关系。
- **Value 析构**：仅 `data->ref--`，**不销毁 data**。data 的销毁由 GC 在 sweep 阶段完成
  （Value 回收后检查其 data 的 ref 是否归 0，归 0 则 `allocator->free(data)`）。
  这样 Data 走 Allocator 且 GC 持 Allocator* 即可释放，Value 无需知道 Allocator。
- **scope_ref**：被几个 scope 跟踪，GC 以 `scope_ref != 0` 为存活根（ADR-012）。

### 3. Data 抽象（引用计数载体）

```cpp
// src/public/core/data.h
namespace neo::core {

class Data {
    int ref_ = 0;  // 多少个 Value 指向此 data
    friend class Value;  // Value 构造/析构操作 ref_
    // 本阶段 Data 是抽象基类，具体扁平类型（NumberData/StringData/...）继承它。
    // data 内存走 Allocator（由 GC 分配/释放），Data 自身不持 Allocator*——
    // GC 持 Allocator*，sweep 时 ref 归 0 的 data 由 GC 调 allocator->free 释放。
public:
    virtual ~Data() = default;
    // 复杂对象阶段：枚举子 value 供 GC 标记。本阶段扁平 data 无子 value，返回空。
    virtual void enumerate_children(class GCVisitor*) {}
};

}  // namespace neo::core
```

- **ref 由 Value 管理**：Value 构造 `++ref_`，析构 `--ref_`。ref 归 0 时**不自销毁**，
  由 GC 在 sweep 阶段检测到 `ref == 0` 后调 `allocator->free(data)` 释放。
- **为何 GC 销毁 data 而非 Value**：Data 走 Allocator，但 Data 不知道自己的 Allocator*。
  GC 持 Allocator*，是唯一能释放 data 的角色。Value 析构只递减 ref，不释放 data。
- **多 Value 共享 data 的时序**：ref>1 时 GC sweep 一个 Value 不会立即 free data，
  只有最后一个引用该 data 的 Value 被回收、ref 归 0 时才 free。GC sweep 逐个 Value
  析构（ref--），析构后检查该 data 的 ref，归 0 则立即 free（无需二次扫描）。
- **enumerate_children**：复杂对象阶段由具体 data 覆写，向 GCVisitor 报告子 value；本阶段基类默认空实现。

### 4. GCEngine 核心接口

```cpp
// src/public/core/gc.h
namespace neo::core {

class GCVisitor {
public:
    virtual ~GCVisitor() = default;
    // 标记一个 value 存活，并沿其 data 子 value 递归标记（本阶段 data 扁平，递归 noop）
    virtual void visit(Value* v) = 0;
};

class GCEngine {
public:
    virtual ~GCEngine() = default;

    // 创建 Value：分配内存 + placement new 构造 + 登记到全局 value 表。
    // 自动 GC：检查双重阈值（内存/数量，见 §6）超限先 collect 再分配。
    // 返回 nullptr 表示 OOM（Allocator 配额耗尽且 GC 后仍不够），
    //   由 Context 封装为 OOM Exception（业务哨兵，非此处的 nullptr）。
    // 注：data 由调用方通过 alloc_data<T> 预先分配，create_value 只接管指针。
    virtual Value* create_value(Type* type, Data* data) = 0;

    // 分配 Data 内存：走 Allocator + placement new。调用方用
    //   auto* d = gc->alloc_data<NumberData>(42);
    // 拿到 Data*，再 create_value(type, d)。data 的释放由 GC sweep 负责
    // （ref 归 0 时 allocator->free），调用方不释放。
    // 返回 nullptr 表示 OOM。
    template <typename T, typename... Args>
    T* alloc_data(Args&&... args);

    // 显式触发 GC（测试/手动控制用）
    virtual void collect() = 0;

    // —— 装饰性查询接口 ——
    virtual size_t value_count() const = 0;       // 当前存活的 Value 总数
    virtual size_t collected_count() const = 0;   // 累计已回收的 Value 数（统计用）
    virtual void   enumerate_values(GCVisitor* v) const = 0;  // 遍历所有存活 Value

    // —— GC 触发阈值配置 ——
    virtual void set_mem_threshold(double ratio) = 0;    // 内存阈值（默认 0.8）
    virtual void set_count_threshold(size_t count) = 0;  // Value 数量阈值（默认 10000）
};

// 工厂：创建 GC 引擎，注入 Allocator（ADR-008 GC→Allocator 依赖方向）
std::unique_ptr<GCEngine> create_gc_engine(infra::Allocator* alloc);

}  // namespace neo::core
```

#### create_value 流程

```
create_value(type, data):
  1. 检查是否需触发 GC（双重阈值，见 §6）：
     - 内存阈值：limit_ != SIZE_MAX && used_/limit_ >= mem_threshold_
     - 数量阈值：value_count_ >= count_threshold_
     任一满足先 collect()（Mark-Sweep 回收不可达 Value，释放内存给 Allocator）
  2. p = allocator->alloc(sizeof(Value))
     → nullptr：OOM，返回 nullptr（GC 后仍不够，Context 转为 OOM Exception）
  3. placement new (p) Value(type, data)
     → Value 构造内 data->ref++
  4. 登记到 values_ 表（GC 持有所有 Value 的跟踪表，collect 时遍历）
  5. 返回 Value*（裸指针，调用方通过 Scope 跟踪）
```

### 5. GC 标记-清除算法（Mark-Sweep 同步回收）

```
collect():
  —— 标记阶段 ——
  1. 所有 Value 的 marked_ = false
  2. 遍历 values_，对 scope_ref_ != 0 的 Value 调 visitor.visit(v)
     → visit 内：v->marked_ = true，并沿 v->data_->enumerate_children(visitor) 递归标记子 value
     → 本阶段 data 扁平，enumerate_children noop，仅根被标记
  —— 清除阶段 ——
  3. 遍历 values_，未 marked_ 的 Value：
     a. 析构 Value（~Value 内 data->ref--，但不释放 data）
     b. 检查该 Value 原 data 的 ref：若 ref == 0，allocator->free(data)
        （多 Value 共享 data 时，仅最后一个引用被回收才 free data）
     c. allocator->free(value)
     d. 从 values_ 移除
  4. 累计 collected_count_ += 本次回收数
```

- **同步回收**：collect 在标记后立即 sweep，未标记 Value 立即析构+释放，不引入 freelist 复用。
- **data 释放由 GC 负责**：Value 析构仅 `data->ref--`，GC 在 sweep 该 Value 后检查 data 的 ref，
  归 0 则 `allocator->free(data)`。这样 Data 走 Allocator 但无需持 Allocator*，GC 是唯一释放者。
- **多 Value 共享 data**：ref>1 时 sweep 单个 Value 不会 free data，仅递减 ref；最后一个引用
  Value 被回收、ref 归 0 时才 free。无需二次扫描，sweep 单次遍历即可。
- **本阶段遍历 noop**：data 扁平无子 value，标记阶段只标记 `scope_ref != 0` 的根，无递归。

### 6. GC 触发机制（双重阈值）

- **内存阈值**（`mem_threshold_`，默认 0.8）：`create_value` 时检查
  `limit_ != SIZE_MAX && used_/limit_ >= mem_threshold_`，超阈值先 `collect()`。
  无限额（`limit_ == SIZE_MAX`）时此阈值不生效。
- **数量阈值**（`count_threshold_`，默认如 10000）：`create_value` 时检查
  `value_count_ >= count_threshold_`，超阈值先 `collect()`。
  **无限额时此阈值兜底**，确保默认 `create_malloc_allocator()` 场景下 GC 仍能自动触发。
- **阈值可配**：两个阈值均可在工厂或运行时设置。
- **OOM 兜底**：`alloc` 返回 nullptr 时 `create_value` 返回 nullptr。Context 层检查并封装为
  OOM Exception（走 ADR-006 异常即值通道）。此 nullptr 是**合理的 OOM 哨兵**，非业务哨兵。
  nullptr 唯一来源是 Allocator OOM（Value 构造不抛异常，data 有效性由调用方契约保证，ADR-007）。

### 7. Value 与 Data 内存来源（落实 ADR-008）

- **Value 走 Allocator + placement new**：GC 持 `Allocator*`，`alloc(sizeof(Value))` 拿裸内存，
  placement new 构造；sweep 时 `allocator->free`。Value 内存受 Allocator 配额管控，与 GC 联动。
- **Data 走 Allocator，由 GC 的 `alloc_data<T>` 分配**：调用方通过 `gc->alloc_data<T>(args...)`
  分配 data（内部 `allocator->alloc(sizeof(T))` + placement new），拿到 `Data*` 后传给
  `create_value`。data 的释放由 GC sweep 负责（ref 归 0 时 `allocator->free`），调用方不释放。
  本阶段 Data 与 Value 共用同一 Allocator，统一受配额管控。
- **Type 内存**：单例/静态，GC 不管理（ADR-016）。

### 8. Scope 与 Value 的跟踪关系（落实 ADR-012）

```cpp
// src/public/core/scope.h（本阶段或后续 ADR 详化）
class Scope {
    std::vector<Value*> values_;        // 跟踪的 value（含匿名临时值）
    std::unordered_map<std::string, Value*> named_values_;  // 命名绑定（借用）
public:
    void add(Value* v) { values_.push_back(v); v->scope_ref_inc(); }
    void add_named(std::string name, Value* v) { named_values_[name] = v; /* scope_ref 由 add 负责 */}
    // 作用域退出：遍历 values_ 逐个 scope_ref_dec（ADR-012 §5）
    void on_exit() { for (auto* v : values_) v->scope_ref_dec(); values_.clear(); named_values_.clear(); }
};
```

- **无孤立 value 不变量**：Value 创建后必须 `scope.add(v)`，否则 scope_ref=0 会被下次 GC 回收
  （ADR-012 §2）。临时值（如 `1+1` 的操作数与结果）也加入当前 scope。
- **scope_ref_inc/dec 由 Scope 负责**，GC 只读 scope_ref 做根判定。

## 接口-实现-工厂（落实 ADR-010）

| 组件 | public（接口/公开类） | private（实现） | 工厂 |
|------|----------------------|----------------|------|
| Value | `core/value.h` Value 类 | — | — |
| Data | `core/data.h` Data 抽象基类 | 具体扁平类型（NumberData 等） | — |
| GCEngine | `core/gc.h` GCEngine 接口 + GCVisitor | private/core/gc_engine.cpp GCEngineImpl | `create_gc_engine(Allocator*)` |

- Value/Data 是公开类（非接口，本阶段具体类，后续 Data 子类化）。
- GCEngine 是抽象接口，实现类 GCEngineImpl 在 private，工厂注入 Allocator。

## 模块组织

```
src/public/core/
  value.h        Value 类（meta+data+scope_ref）
  data.h         Data 抽象基类（ref + enumerate_children）
  gc.h           GCEngine 接口 + GCVisitor + 工厂声明
src/private/core/
  impl_gc.h      GCEngineImpl 声明
  gc_engine.cpp  GCEngineImpl 实现（create_value/collect/标记清除）
tests/unit/core/
  gc_test.cpp    GC 生命周期测试
  value_test.cpp Value/Data 引用计数测试
  scope_test.cpp Scope 跟踪与 scope_ref 测试
```

## 测试策略

- **Value/Data 引用计数**：构造 ref++、析构 ref-- 归 0 销毁、多 Value 共享 data 时 ref>1。
- **Scope 跟踪**：add 后 scope_ref==1、on_exit 后 scope_ref==0、named_values 借用不影响 ref。
- **GC 标记回收**：scope_ref!=0 存活、scope_ref==0 回收、collect 后 value_count 递减。
- **自动触发**：配额超阈值时 create_value 触发 collect、回收后继续分配。
- **OOM**：配额耗尽且 GC 后仍不够，create_value 返回 nullptr。
- **临时值回收**：表达式临时值在作用域退出后 scope_ref=0，下次 GC 回收。
- **图遍历 noop**：本阶段 data 无子 value，enumerate_children 不报子 value，仅根被标记。

## 后果

- 优点：生命周期管线完整（创建→跟踪→标记→回收）、GC 与 Allocator 配额联动拦截 OOM、
  引用计数分层销毁 data 确定性、图遍历框架为复杂对象扩展预留。
- 缺点：Mark-Sweep 同步回收有停顿（本阶段可接受）、Value 走 Allocator 有 placement new 复杂度、
  每次分配检查阈值有微小开销。
- 本阶段限制：仅扁平 Value、无复杂对象图、无分代/增量 GC、无弱引用/终接器。

## 后续扩展（非本阶段）

- 复杂对象（Object/Array/Function）：data 持有子 value，enumerate_children 报告子 value，
  GC 图遍历真实生效，处理对象图循环。
- 分代/增量 GC：减少停顿。
- 弱引用/终接器：WeakRef/FinalizationRegistry。
- Data 内存统一走 Allocator。
