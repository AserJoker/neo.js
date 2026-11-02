# ADR-012: 核心语言模型与 GC 的关系 — Scope 树 + scope_ref

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-011（Engine 组织）、ADR-008（内存架构）、ADR-002（GC 抽象）、ADR-015（Value 内存模型）

## 背景

确定核心语言模型（Context/Scope/Value）与 GC 的关系。

## 决策

### 1. 采用 Scope 树

Context 持有三个特殊 Scope：
- **global**：Scope 树的根。
- **root**：当前模块的根，parent 一定是 global。
- **current**：当前作用域。

### 2. Value 由 GC 引擎创建，GC 持有所有权

- 不存在孤立 value：任何 value（含表达式临时值）都至少被一个 scope 持有。
- 如 `1+1` 表达式在当前作用域产生 **3 个匿名 value**（两个操作数 + 结果）。

### 3. Scope 内部两个 value 容器

Scope 维护：
- **values[]**：数组，表示当前作用域跟踪了哪些 value。
- **named_values**：名字→value 映射，跟踪哪些 value 有名字（借用，不拥有）。

### 4. scope_ref 引用计数

- Value 内部有 **scope_ref** 计数器：被几个 scope 持有。
- **value 加入 scope 时 scope_ref++**（被跟踪）。

### 5. 作用域退出释放（关键时机）

- **作用域退出时，遍历自己的所有 values，逐个 scope_ref--**。

### 6. GC 标记：根 + 图遍历（标准追踪式）

- GC 持有全局所有 value 的所有权，value 通过 GC 引擎创建。
- **GC 发生时：遍历所有 value，以 scope_ref != 0 作为存活根进行标记**，
  再沿对象引用链（对象→属性值）遍历标记所有可达 value。
- 未被标记的 value 回收。
- 临时/匿名 value 留到 GC 时回收（不立即释放）。

## Value 生命周期

```
Value 创建（GC 引擎创建，scope_ref = 0）
  → 加入 scope（scope.values，scope_ref++）
  → 作用域存活：被 scope 持有
  → 作用域退出：遍历 values，逐个 scope_ref--
  → GC：scope_ref != 0 为根 + 图遍历标记
  → 未标记的回收
```

## 后果

- 优点：不存在孤立 value（正确性）、scope 退出自然递减（生命周期清晰）、
  GC 以 scope_ref 为根 + 图遍历（标准、可靠）。
- 缺点：每个 value 有 scope_ref 计数开销；作用域退出需遍历 values。
