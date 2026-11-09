# ADR-040: AggregateError — 复用 Error 族 + IterableToList

- 状态: 已接受 (2026-11-09)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-038（Error 基类与子类族）、ADR-039（错误类型接入）、ADR-018（对象模型）

## 背景

实现 ES2024 §20.5.7 的 `AggregateError`（聚合错误）。

## 决策

### 1. 不新增 TypeKind，复用 Error 族

`AggregateError` 的实例描述与其它 Error 子类完全一致——都是带 `[[ErrorData]]`
内部槽的对象，靠原型链区分。故沿用 ADR-038 的子类族模式：

- 共用 `TypeKind::Error` + `ErrorData`（空标记，无新字段）；
- 原型是**普通对象**（无 `[[ErrorData]]`，§20.5.7.2），原型链 → `%Error.prototype%`；
- 自有 `name = "AggregateError"`、双向 `constructor`；
- 注册走既有 `registerNativeError`，与 5 个一般性错误子类同一路径。

### 2. [[AggregateErrors]] 不落 data 字段

规范中 `[[AggregateErrors]]` 内部槽**没有任何读取操作**，只在构造时用于填充
实例的自有 `errors` 属性。因此不新增 Data 字段，`errors` 就是普通自有属性
（writable / 不可枚举 / 可配置），属性语义完全走对象模型。

### 3. 构造函数（§20.5.7.1）

```
AggregateError(errors, message):
  1. errors 缺失 → undefined；IterableToList(§7.4.2.1) 收集（不可迭代 → TypeError）
  2. message 按 NativeError 规则取（undefined 时不定义属性）
  3. createError(message, "AggregateError") 建实例（stack 首行用该名）
  4. 原型改挂 AggregateError.prototype
  5. 定义自有 errors = 收集到的元素数组
```

### 4. 新增规范原语 IterableToList

`detail::iterableToList(ctx, items, out)`（TypeBehavior.h）——取 `@@iterator`，
逐个 `next` 收集 `value` 直到 `done`；异常路径按 §7.4.7 调 `IteratorClose`
（已发生的异常优先于 `return()` 的错误）。放在 `iteratorClose` 之后定义。

该原语是 Map/Set 构造、`Array.from` 等后续功能的公共依赖。

## 后果

- 优点：零新增类型/分派点；`instanceof`、`Object.prototype.toString`、
  `toString` 全部自然继承；`errors` 属性语义正确。
- 缺点：`errors` 数组在构造时快照，之后修改 `err.errors` 不影响内部（规范如此）。
- 范围：`SuppressedError`（抑制错误）仍不在本阶段范围。
