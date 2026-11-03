# ADR-030: M2 里程碑 — 基本 Engine 框架支持 GC

- 状态: 已接受 (2026-11-03)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-008（Allocator/GC 分层）、ADR-010（接口-实现-工厂）、ADR-011（Engine 组织）、
          ADR-012（Scope 树 + scope_ref）、ADR-013（闭包与三层作用域）、ADR-015（Value meta+data）、
          ADR-016（Type 体系）、ADR-017（类型清单）、ADR-029（Scope-Value-GC 生命周期）

## 背景

M2 目标：基本 Engine 框架支持 GC。把 M1 的基础设施（Allocator/Result）与 ADR-029 的
生命周期管线整合为可运行的 Engine 门面（Context），并落地基础扁平类型，让"创建引擎 →
分配 JS 值 → 作用域跟踪 → 自动 GC 回收"端到端跑通。

M2 采用滚动式里程碑（ADR-026 后约定）：完成一个阶段再定下一个。本 ADR 定义 M2 的阶段
划分与各阶段范围，已完成的第一阶段标 ✅。

## 决策

### M2 阶段划分

| 阶段 | 范围 | 状态 |
|------|------|------|
| 1. Value-Data-GC-Scope 生命周期管线 | 扁平 Value 的创建-跟踪-标记-回收 | ✅ 已完成（ADR-029 落地） |
| 2. Scope 树与三层作用域 | parent 链、global/root/current、on_exit 递归 | 待实现 |
| 3. 基础扁平类型 Type/Data | undefined/null/boolean/number/string | 待实现 |
| 4. Context 门面整合 | 持有 Allocator/GC/Scope 树，创建引擎入口 | 待实现 |
| 5. OOM 异常封装 | create_value nullptr → Context 封装 OOM Exception | 待实现 |

### 阶段 1：Value-Data-GC-Scope 生命周期管线（已完成）

ADR-029 落地。核心组件：
- `Data`（abstract）：ref_ 引用计数，enumerate_children 预留
- `Value`：meta(Type*)+data(Data*)+scope_ref，构造 data->ref++，析构 data->ref--
- `GCEngine`（interface）+ `MarkSweepGC`（impl）：create_value/alloc_data/collect，
  双重阈值自动 GC，sweep 时 data ref 归 0 则 free
- `Scope`：values_+named_values_，add 时 scope_ref++，on_exit 逐个 scope_ref--
- GC 析构契约：final collect 后 values_ 非空则 abort 报告泄漏

测试：27 用例（value_test/gc_test/scope_test），双平台 + ASan 干净。

### 阶段 2：Scope 树与三层作用域

落实 ADR-012 §1（global/root/current）+ ADR-013（闭包三层作用域）。

```cpp
// src/public/core/scope.h（扩展）
class Scope {
    Scope* parent_ = nullptr;              // 父作用域（global 的 parent=nullptr）
    std::vector<Scope*> children_;         // 子作用域（on_exit 时递归释放）
    // values_/named_values_ 同阶段 1
public:
    Scope* parent() const { return parent_; }
    void   add_child(Scope* s) { children_.push_back(s); }

    // 沿 parent 链查找变量（ADR-013 查找路径）
    virtual Value* lookup_chain(std::string_view name) const;

    // 作用域退出：先递归 on_exit 所有子作用域，再释放自己的 values
    void on_exit() {
        for (auto* c : children_) c->on_exit();
        children_.clear();
        for (auto* v : values_) v->scope_ref_dec();
        values_.clear();
        named_values_.clear();
    }
};
```

- **三层作用域**（ADR-012）：Context 持 global（根，永久）/root（模块根，parent=global）/
  current（当前，动态切换）。
- **闭包三层**（ADR-013）：函数调用时 closure_scope → argument_scope → body_scope，
  parent 链连接。本阶段实现 Scope 树骨架，闭包调用流程在后续阶段（函数实现时）补充。
- **on_exit 递归**：作用域退出时先递归释放子作用域，再释放自己的 values。
  对应 ADR-013「作用域析构时递归释放子作用域」。
- **lookup_chain**：沿 parent 链向上查找命名 value（本阶段基础查找，闭包路径在函数阶段补充）。

### 阶段 3：基础扁平类型 Type/Data

落实 ADR-016（Type 体系）+ ADR-017（类型清单）的基础扁平类型部分。

```cpp
// src/public/core/type.h
class Type {
public:
    virtual ~Type() = default;
    // 本阶段 Type 是抽象接口，仅提供类型标识，操作方法（add/eq/...）后续阶段补
    virtual std::string_view name() const = 0;
    // 复杂对象阶段：枚举 data 的子 value 供 GC 标记（本阶段扁平类型无子 value）
    // 注：enumerate_children 在 Data 上，Type 提供类型元信息
};

// src/private/core/types.h（具体 Type 实现声明）
class UndefinedType : public Type { /* name() = "undefined" */ };
class NullType      : public Type { /* name() = "null" */ };
class BooleanType   : public Type { /* name() = "boolean" */ };
class NumberType    : public Type { /* name() = "number" */ };
class StringType    : public Type { /* name() = "string" */ };
// 单例：Type 体系是共享 meta，全局唯一实例
```

基础 Data 子类（在 private/core/）：
- `UndefinedData` / `NullData`：无数据负载（空 data，仅占类型标识）
- `BooleanData`：持 `bool`
- `NumberData`：持 `double`
- `StringData`：持 `std::string`（本阶段字符串值，Unicode 完整支持在后续 Unicode 阶段）

Type 单例获取：`TypeSystem` 持有所有 Type 单例，或全局函数 `get_number_type()` 等。
本阶段 Type 不实现操作方法（add/eq/to_string 等），仅提供类型标识与 name，为后续阶段
的二元运算/单目操作预留。

### 阶段 4：Context 门面整合

落实 ADR-011 §2/§3（Context 门面 + 内存归属）。

```cpp
// src/public/core/context.h
class Context {
public:
    virtual ~Context() = default;

    // 创建 Value 的便捷接口（内部调 gc->create_value + 类型分派）
    // 本阶段提供基础类型创建：create_undefined/create_null/create_boolean/create_number/create_string
    virtual Value* create_undefined() = 0;
    virtual Value* create_boolean(bool b) = 0;
    virtual Value* create_number(double n) = 0;
    virtual Value* create_string(std::string_view s) = 0;
    // ...

    // 作用域访问
    virtual Scope* global() const = 0;
    virtual Scope* root() const = 0;
    virtual Scope* current() const = 0;
    virtual void   push_scope(Scope* s) = 0;  // current 压栈
    virtual void   pop_scope() = 0;           // current 弹栈 + on_exit

    // GC 访问
    virtual GCEngine* gc() const = 0;
    // Allocator 访问（native 层可能需要）
    virtual infra::Allocator* allocator() const = 0;
};

// 工厂：创建 Context，注入 Allocator（或内部创建默认 Allocator）
std::unique_ptr<Context> create_context();
std::unique_ptr<Context> create_context(infra::Allocator* alloc);
```

- **Context 持有 Allocator/GC/Scope 树生命周期**（ADR-011 §3）：
  Allocator 外部注入或内部创建默认，GC 由 Context 创建（注入 Allocator），Scope 树
  由 Context 持有 global/root/current。
- **接口-实现-工厂**（ADR-010）：Context 抽象接口在 public/core/，实现 ContextImpl
  在 private/core/，工厂 create_context() 返回接口指针。
- **create_* 便捷接口**：封装 gc->alloc_data<T> + gc->create_value(type, data)，
  按类型分派到对应 Type 单例 + Data 子类。调用方无需直接接触 GC/Type/Data 细节。
  始终返回 `Value*`（不返回 Result，阶段 5 落实 OOM 时返回预分配 OOM Exception 单例）。
- **预分配 OOM Exception**：Context 初始化时最早分配一个 Exception Value 挂载到
  GlobalScope（单例，GC 永不回收），create_* 内部 OOM 时返回它（详见阶段 5）。

### 阶段 5：OOM 异常封装

落实 ADR-006（异常即值）+ ADR-029 §6（OOM 哨兵）+ ADR-017（Exception 类型）。

**核心原则：Engine 层完全用语言特性表达错误，返回值必须为 `Value*`。**
不引入 `Result<T>` 到 Engine API——OOM 用预分配的 Exception Value 表达。

#### OOM Exception Value 预分配

- Context 初始化时（Allocator 配额充足），**最早分配一个 Exception 类型的 Value**
  作为 OOM 哨兵，挂载到 GlobalScope（第一个预分配的错误 Value）。
- 该 Value **单例**：所有 OOM 都返回同一实例，调用方可指针比较判断是否 OOM。
- 挂载到 GlobalScope 意味着 `scope_ref >= 1`，GC 永不回收它（全局可达）。
- **分配失败直接 panic**：若 Context 初始化时连 OOM Exception Value 都分配不出来
  （`gc->create_value` 返回 nullptr），说明系统不可用，`abort()`。

```cpp
// Context 初始化（伪代码）
ContextImpl::ContextImpl(...) {
    global_scope_ = /* 创建 global scope */;
    // 最早分配 OOM Exception Value，挂到 global
    oom_value_ = gc_->create_value(exception_type(), oom_data());
    if (oom_value_ == nullptr) {
        // 连 OOM 哨兵都分配不出，系统不可用
        std::abort();
    }
    global_scope_->add(oom_value_);
    global_scope_->bind("__oom__", oom_value_);  // 命名挂载（可选）
}
```

#### create_* 接口返回 Value*

- Context 的 `create_number/create_string/...` 始终返回 `Value*`，**不返回 Result**。
- 内部 `gc->create_value` 返回 nullptr 时，Context 返回 `oom_value_`（预分配单例）。
- 调用方检查：`if (v == ctx->oom_value()) { /* OOM 处理 */ }`。

```cpp
Value* ContextImpl::create_number(double n) {
    auto* d = gc_->alloc_data<NumberData>(n);
    if (d == nullptr) return oom_value_;
    auto* v = gc_->create_value(number_type(), d);
    if (v == nullptr) {
        gc_->free_data(d);  // data 已分配但 create_value 失败，需释放
        return oom_value_;
    }
    return v;
}
```

#### 为何不用 Result<T>

- Engine 层是语言运行时，错误必须用语言自身的值类型（Value/Exception）表达，
  才能被脚本代码捕获处理。Result<T> 是 C++ 层机制，泄漏到 Engine API 会让
  native 代码与脚本错误处理割裂。
- ADR-006 异常即值：异常是带标记的 Value，走统一 return 通道。OOM 作为异常的一种，
  同样是 Value，符合"异常即值"。
- 后续 VM/解释器执行脚本时，OOM Value 经 ADR-006 通道转为脚本可见的 Exception。

#### data 释放问题

- `gc->alloc_data` 成功但 `gc->create_value` 失败时，data 未被 Value 引用，GC 无法
  回收（GC 只跟踪 Value）。Context 需手动释放 data。
- 本阶段 GC 补充 `free_data(Data*)` 接口：显式释放未注册的 data（调
  `data->~Data()` + `allocator->free(data)`）。仅在 create_value 失败时由 Context 调用。

#### 测试

- 配额耗尽时 create_* 返回 oom_value_（单例），不返回 nullptr、不崩溃。
- oom_value_ == ctx->oom_value() 指针比较成立。
- Context 初始化时 oom_value_ 已挂载到 global，GC collect 不回收它。
- 连 oom_value_ 都分配不出（极小配额）时 Context 创建 abort。

## 模块组织（M2 整体）

```
src/public/core/
  value.h        Value 类（已完成）
  data.h         Data 抽象基类（已完成）
  gc.h           GCEngine 接口 + GCVisitor（已完成）
  scope.h        Scope 类（阶段 2 扩展 parent 链）
  type.h         Type 抽象接口（阶段 3 新增）
  context.h      Context 抽象接口 + 工厂（阶段 4 新增）
src/private/core/
  value.cpp      Value 构造/析构（已完成）
  impl_mark_sweep_gc.h + mark_sweep_gc.cpp  MarkSweepGC（已完成）
  impl_context.h + context.cpp              ContextImpl（阶段 4 新增）
  types.h + types.cpp                       具体 Type + Data 子类（阶段 3 新增）
tests/unit/core/
  value_test.cpp / gc_test.cpp / scope_test.cpp  （已完成）
  type_test.cpp / context_test.cpp               （阶段 3/4 新增）
  test_data.h                                    （已完成，阶段 3 后可移除）
```

## 测试策略（M2 各阶段）

- **阶段 2**：Scope parent 链、children 递归 on_exit、lookup_chain 沿链查找、
  global/root/current 三层切换。
- **阶段 3**：各 Type 的 name()、各 Data 子类的数据存取、create_number/create_string
  返回正确类型与值、Type 单例唯一性。
- **阶段 4**：Context 创建后 global/root/current 就绪、create_* 返回有效 Value、
  push_scope/pop_scope 联动 current、pop_scope 触发 on_exit + GC 回收。
- **阶段 5**：配额耗尽时 create_* 返回 OOM 错误码、GC 后配额恢复可继续创建、
  OOM 不崩溃、Result 通道正确传播。

## 后果

- 优点：M2 结束时 Engine 框架可用——创建 Context、分配基础 JS 值、作用域跟踪、
  自动 GC 回收端到端跑通；OOM 受控不拖垮宿主；为 M3（复杂对象/函数/字节码执行）奠基。
- 缺点：本阶段仅扁平类型无对象/函数、无脚本执行、无运算操作；Type 体系仅类型标识
  无行为方法；Scope 树骨架在但闭包调用流程待函数阶段。
- M2 完成后下一里程碑（M3）由项目负责人后续定义（预计：复杂对象/函数/字节码 VM）。
