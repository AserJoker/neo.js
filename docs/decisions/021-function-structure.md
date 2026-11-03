# ADR-021: Function 对象结构 — 二元 tag union + 闭包归属

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-013（闭包与作用域）、ADR-015（Value 内存模型）、ADR-018（对象模型）、ADR-005（native 等价性）

## 背景

确定 Function 对象的内部结构，及其与闭包作用域、GC 的关系。

## 决策

### 1. Function 结构

```
Function
├── 类似对象的字段（properties/order/symbols/prototype，继承对象模型 ADR-018）
├── closure_scope（闭包作用域）
└── 二元 tag union（kind + union）
    ├── kind: native / script
    └── union:
        ├── native: native 函数指针（native_fn）
        └── script: module id + pc（指向字节码位置）
```

### 2. 二元 tag union

- `kind` 区分 **native / script**。
- `union` 承载对应数据：
  - **native**：native 函数指针（`native_fn`）。
  - **script**：`module id + pc`（指向字节码指令位置）。

### 2b. 可选的 self 指针（this 绑定）

- FunctionData 中还有一个**可选的 self 指针**，用于 this 绑定。
- **bind()**：`f.bind(obj)` 产生新函数，self = obj（调用时 this 固定）。
- **箭头函数**：底层实现上**箭头函数就是自动 bind 的函数**——
  箭头函数定义时捕获外层 this，等价于创建时 self 已绑定。
- 调用时：若有 self 指针，this = self；否则按普通规则确定 this。

### 3. 两者都支持 closure_scope

- 无论 native 还是 script 函数都可以有闭包作用域。

### 4. 闭包变量不需要 GC 主动标记

- 闭包变量被 **closure_scope 持有**（scopeRef）→ 必定存活。
- **不需要从 Function 出发去标记 Function 的闭包**（GC 不把闭包当 Function 的子变量）。
- 底层实现：**闭包变量属于 closure_scope，不是 Function 的子变量**。
- 因此 GC 遍历 Function 时**不需要（也不应该）遍历它的闭包作用域**；
  闭包变量的存活由 closure_scope 的持有保证（scopeRef 根判定）。

## 推论

- Function → closure_scope 的引用不需要被 GC 追踪（GC 优化点）。
- native/script 函数统一持 closure_scope，与 ADR-005（native/脚本等价）一致。

## 后果

- 优点：native/script 统一结构；闭包归属清晰；GC 少一条遍历边。
- 缺点：Function 需维护 tag union 与 closure_scope。
