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
- **OOM 检查**：create_* 返回 nullptr 时，本阶段 Context 内部检查并转为错误
  （阶段 5 落实 OOM Exception 封装）。

### 阶段 5：OOM 异常封装

落实 ADR-006（异常即值）+ ADR-029 §6（OOM 哨兵）。

- `create_value` 返回 nullptr（Allocator 配额耗尽且 GC 后仍不够）时，Context 的
  create_* 接口检测到 nullptr，封装为 OOM Exception（走 Result<T> 错误通道）。
- 本阶段 create_* 接口签名调整：返回 `Result<Value*>` 而非 `Value*`，OOM 时返回
  `Result<Value*>::err(ErrCode::OutOfMemory)`。
- 后续 VM/解释器执行脚本时，OOM 走 ADR-006 异常即值通道，转为脚本可见的异常。
- 测试：配额耗尽时 create_* 返回 OOM 错误码，不崩溃。

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
