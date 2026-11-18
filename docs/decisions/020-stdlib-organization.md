# ADR-020: 标准库组织 — 全局变量注册

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-011（Engine 组织）、ADR-012（Scope 树）、ADR-005（native 等价性）

## 背景

确定 ECMAScript 标准库的组织方式。

## 决策

### 1. 标准库是全局变量

- JavaScript 的标准库都是全局变量（符合 JS 规范——标准库是全局对象的属性）。

### 2. Context 初始化时注册到全局对象

- Context 初始化时创建 GlobalScope（全局环境记录，Scope 树的根）与唯一的
  global object（ADR-033「全局环境记录：对象记录 + 声明记录」）。
- 内建对象/函数通过 `Context::defineGlobal` 定义为 **global object 的属性**
  （对象记录），不再写入 GlobalScope 的名字映射：

```
Object / Array / String / Number / Boolean / Math / JSON / Date / RegExp /
Promise / Map / Set / Symbol / Reflect / Proxy / ...
parseInt / parseFloat / isNaN / isFinite / encodeURI* / decodeURI* / ...
```

- 顶层 `var`/函数声明同样落 global object 属性；顶层 `let`/`const`/`class`
  留在 GlobalScope 的声明记录（ADR-033）。
- 属性特性按规范：内建函数 writable/不可枚举/可配置；`undefined`/`NaN`/
  `Infinity` 只读/不可枚举/不可配置。

### 3. 标准库实现为 native 方法

- 标准库函数实现为 native 函数（符合 ADR-005：native/脚本等价）。
- 宿主可额外注册自定义全局/模块（可嵌入需求）。

## 与既有模型的关系

- GlobalScope：Scope 树根，兼对象记录与声明记录（ADR-012 / ADR-033）。
- global object：内建与顶层 var/函数声明的属性容器。
- native 等价：标准库函数 = native 函数，经 Engine API 实现（ADR-005）。

## 后果

- 优点：组织简单统一、符合 JS 规范、与 Scope 机制天然衔接、宿主可扩展。
- 缺点：标准库量大，需逐个实现（工作量在实现层，不在组织层）。
