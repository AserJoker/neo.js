# ADR-020: 标准库组织 — 全局变量注册

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-011（Engine 组织）、ADR-012（Scope 树）、ADR-005（native 等价性）

## 背景

确定 ECMAScript 标准库的组织方式。

## 决策

### 1. 标准库是全局变量

- JavaScript 的标准库都是全局变量（符合 JS 规范——标准库是全局对象的属性）。

### 2. Context 初始化时注册到 globalScope

- Context 初始化时创建 globalScope（全局作用域，Scope 树的根）。
- 把所有标准库对象/函数注册到 globalScope 的 named_values：

```
Object / Array / String / Number / Boolean / Math / JSON / Date / RegExp /
Promise / Map / Set / Symbol / Reflect / Proxy / ...
parseInt / isNaN / ...
```

- 通过 globalScope 的 named_values（名字→value 映射）注册，走既有 Scope 机制。

### 3. 标准库实现为 native 方法

- 标准库函数实现为 native 函数（符合 ADR-005：native/脚本等价）。
- 宿主可额外注册自定义全局/模块（可嵌入需求）。

## 与既有模型的关系

- globalScope：Scope 树根（ADR-012）。
- named_values：名字→value 映射（ADR-012）。
- native 等价：标准库函数 = native 函数，经 Engine API 实现（ADR-005）。

## 后果

- 优点：组织简单统一、符合 JS 规范、与 Scope 机制天然衔接、宿主可扩展。
- 缺点：标准库量大，需逐个实现（工作量在实现层，不在组织层）。
