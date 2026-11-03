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
| 5. OOM Fatal 封装 | create_value nullptr → Context 封装 OOM Fatal，透传契约 | 待实现 |

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
enum class TypeKind : uint8_t {
    Undefined, Null, Boolean, Number, String,
    Exception, Fatal, Interrupt,
    // Object/Function/Array/... 后续阶段
};

class Type {
    TypeKind kind_;
public:
    TypeKind kind() const { return kind_; }
    virtual const char* name() const = 0;

    // 行为接口——统一返回 Value*（正常/Exception/Fatal 同一通道，ADR-006 + ADR-017 §4）
    // 本阶段仅实现基础运算，复杂操作（get_property 等）后续阶段补
    virtual Value* add(Context* ctx, Value* a, Value* b) = 0;
    virtual Value* sub(Context* ctx, Value* a, Value* b) = 0;
    virtual Value* to_string(Context* ctx, Value* self) = 0;
    // ... 其余基础运算
};

// src/private/core/types.h（具体 Type 实现声明）
class UndefinedType : public Type { /* name()="undefined", 兜底行为 */ };
class NullType      : public Type { /* name()="null",    兜底行为 */ };
class BooleanType   : public Type { /* name()="boolean" */ };
class NumberType    : public Type { /* name()="number",  按 JS 规则实现 add/sub/... */ };
class StringType    : public Type { /* name()="string",  按 JS 规则实现 +/concat */ };
// 单例：Type 体系是共享 meta，全局唯一实例，Value 持 Type* 共享无须 ref 计数
```

基础 Data 子类（在 private/core/）：
- `UndefinedData` / `NullData`：无数据负载（空 data，仅占类型标识）
- `BooleanData`：持 `bool`
- `NumberData`：持 `double`
- `StringData`：持 `std::string`（本阶段字符串值，Unicode 完整支持在后续 Unicode 阶段）

Type 单例获取：`TypeSystem` 持有所有 Type 单例，或全局函数 `get_number_type()` 等。

本阶段 Type 行为实现范围（落实 ADR-016 修订）：
- **分派规则**：`ctx->add(a,b)` → `a->type()->add(ctx, a, b)`，Context 纯代理不持规则表。
- **跨类型转换**：左 type 按所实现操作的 JS 规则主动调换左右（`ctx->add(b,a)`），
  不是"处理不了"的被动 fallback。例：`1 + ""` → NumberType 按 JS `+` 规则调换为
  `ctx->add("", 1)` → StringType 落实 concat。
- **兜底分支**：无法处理的组合返回 NaN 或 Exception（Type 内部 `ctx->create_exception(...)`）。
- **操作有效性**：Type 自负责，如 `NumberType::call` 返回 "not a function" Exception。
- **本阶段不实现 ToPrimitive**：基础类型间转换靠调换 + type 自己的简单转换，
  ToPrimitive 留到对象阶段。

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
  始终返回 `Value*`（不返回 Result，阶段 5 落实 OOM 时返回预分配 OOM Fatal 单例）。
- **预分配 OOM Fatal**：Context 初始化时最早分配一个 Fatal Value 挂载到
  GlobalScope（单例，Context 存活期间被 GlobalScope 跟踪不回收），create_* 内部 OOM 时返回它（详见阶段 5）。

### 阶段 5：OOM Fatal 封装

落实 ADR-006（异常即值）+ ADR-029 §6（OOM 哨兵）+ ADR-017（Fatal 类型）。

**核心原则：Engine 层完全用语言特性表达错误，返回值必须为 `Value*`。**
不引入 `Result<T>` 到 Engine API——OOM 用预分配的 Fatal Value 表达。

OOM 属于 **Fatal**（不可捕获致命错误，见 ADR-017 §4），不是 Exception。
Exception 可被脚本 `try/catch` 捕获；Fatal 不可捕获，必须透传到最外层调用方
（CLI 或嵌入宿主）。

#### OOM Fatal Value 预分配

- Context 初始化时（Allocator 配额充足），**最早分配一个 Fatal 类型的 Value**
  作为 OOM 哨兵，挂载到 GlobalScope（第一个预分配的错误 Value）。
- 该 Value **单例**：所有 OOM 都返回同一实例，调用方可指针比较判断是否 OOM。
- 挂载到 GlobalScope 意味着 `scope_ref >= 1`，Context 存活期间被 GlobalScope 跟踪，
  GC 不会回收它。Context 析构时 GlobalScope on_exit（scope_ref 归 0），GC final
  collect 时自然回收——与普通 Value 生命周期一致，无特殊处理。
- **分配失败直接 panic**：若 Context 初始化时连 OOM Fatal Value 都分配不出来
  （`gc->create_value` 返回 nullptr），说明系统不可用，`abort()`。

```cpp
// Context 初始化（伪代码）
ContextImpl::ContextImpl(...) {
    global_scope_ = /* 创建 global scope */;
    // 最早分配 OOM Fatal Value，挂到 global
    oom_value_ = gc_->create_value(fatal_type(), oom_data());
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

#### Fatal 透传契约

OOM 是 Fatal，不可捕获。一旦 OOM Fatal Value 被产生，所有调用方必须**直接透传，
不处理不转换不包装**，一路返回到最外层调用方（CLI 或嵌入宿主）。详见 ADR-017 §4。

**适用范围：所有返回 `Value*` 的业务接口——无一例外。** 不只是 native 函数和 VM 执行
循环，理论上任何返回 `Value*` 的接口（Context 的 `create_*`、Sema/Compiler 内部能产生
Value 的路径、native 函数、VM 指令执行……）都可能返回 Fatal。因此**每个调用点**在调用
返回 `Value*` 的接口后，都必须检查返回值是否为 Fatal，是则立即 return 透传。这是一条
无例外的纪律——只要接口签名是 `Value*`，调用点就必须做 Fatal 检查。

- **通用规则**：调用任何返回 `Value*` 的接口后，检查 `v->type() == fatal_type()`
  （或 `v == ctx->oom_value()` 指针比较），是则立即 return 透传，不处理不转换不包装。
- **native 函数**：调用其他函数/接口后检查返回值，Fatal 则立即 return 透传。
- **VM 执行循环**：每条指令的执行结果检查，Fatal 则跳出执行循环，直接返回给调用方。
- **最外层处理**：CLI/宿主用**额外内存**（宿主自己的内存管理，不经过 Engine 的
  Allocator；CLI 可用预先创建的栈上空间）解析错误——拼接错误消息、打印调用栈等。
  因 Fatal 发生时 Engine 内存可能已耗尽，不能用 Engine 的 Allocator 分配诊断信息。
  最外层是 Fatal 透传链的**唯一终点**，只有它允许（也必须）处理 Fatal。

**为何 Fatal 不可捕获**：OOM 发生时 Engine 内存已耗尽，脚本 `try/catch` 的 catch
块本身可能再次分配（构造错误对象、调用 handler）而触发二次 OOM，无法恢复。故 OOM
必须直接透传到能用额外内存处理的宿主层。

#### 为何不用 Result<T>

- Engine 层是语言运行时，错误必须用语言自身的值类型（Value/Fatal）表达，
  才能被宿主层统一处理。Result<T> 是 C++ 层机制，泄漏到 Engine API 会让
  native 代码与运行时错误处理割裂。
- ADR-006 异常即值：异常是带标记的 Value，走统一 return 通道。OOM 作为 Fatal 的一种，
  同样是 Value，符合"异常即值"。
- 后续 VM/解释器执行脚本时，OOM Fatal Value 经 ADR-006 通道直达最外层（不被
  `try/catch` 拦截），由宿主兜底处理。

#### 为何 OOM 是 Fatal 而非 Exception

- Exception 是可恢复的业务异常，脚本 `try/catch` 可捕获处理（ADR-017 §3）。
- OOM 是不可恢复的系统级故障，脚本无法处理，宿主兜底（ADR-017 §4）。
- 若 OOM 是 Exception，脚本 `try/catch` 可捕获，但 catch 块本身可能再次 OOM，
  无法保证恢复语义。归为 Fatal 强制透传，避免此问题。

#### data 释放问题

- `gc->alloc_data` 成功但 `gc->create_value` 失败时，data 未被 Value 引用，GC 无法
  回收（GC 只跟踪 Value）。Context 需手动释放 data。
- 本阶段 GC 补充 `free_data(Data*)` 接口：显式释放未注册的 data（调
  `data->~Data()` + `allocator->free(data)`）。仅在 create_value 失败时由 Context 调用。

#### 测试

- 配额耗尽时 create_* 返回 oom_value_（单例），不返回 nullptr、不崩溃。
- oom_value_ == ctx->oom_value() 指针比较成立。
- oom_value_->type() == fatal_type()（类型为 Fatal，非 Exception）。
- Context 初始化时 oom_value_ 已挂载到 global（scope_ref>=1），Context 存活期间
  GC collect 不回收它；Context 析构随 global on_exit 自然回收。
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
