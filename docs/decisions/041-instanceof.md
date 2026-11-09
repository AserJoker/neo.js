# ADR-041: instanceof — @@hasInstance 协议 + OrdinaryHasInstance

- 状态: 已接受 (2026-11-09)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-018（对象模型/原型链）、ADR-034（原型-构造函数）、ADR-038（Error 族）

## 背景

ADR-034 明确「无 `instanceof`：需 `Symbol.hasInstance` 与原型链遍历，后续阶段」。
现补齐该操作。

## 决策

### 1. 两个原语（TypeBehavior.h）

**`instanceOf(ctx, value, target)`** —— `value instanceof target`（§13.10.2）：

1. `target` 非对象 → TypeError
2. 取 `target[@@hasInstance]`；非 null/undefined 时：
   - 不可调用 → TypeError
   - 以其调用结果的 **ToBoolean** 为准（不要求返回 Boolean）
3. 否则 `target` 须可调用，否则 TypeError
4. 走 `OrdinaryHasInstance`

**`ordinaryHasInstance(ctx, ctor, obj)`** —— §7.3.21：

1. `ctor` 不可调用 → false
2. `ctor` 是绑定函数 → 以 `[[BoundTargetFunction]]` 递归
3. `obj` 非对象 → false
4. `P = ctor.prototype`，非对象 → TypeError
5. 沿 `obj` 的原型链找 `P`

### 2. 对外入口

`IContext::instanceOf(value, target)` —— 纯代理到 `detail::instanceOf`。
VM 的 `instanceof` 指令直接调用它。

### 3. Function.prototype[@@hasInstance]

按 §20.2.3.6 注册：直接走 `OrdinaryHasInstance`（**不**查 `@@hasInstance`，
避免无限递归）。

描述符：`writable=false / enumerable=false / configurable=false`——规范明确
不可配置，故 `delete Function.prototype[Symbol.hasInstance]` 失败。

### 4. 绑定函数

`instanceof` 对绑定函数按 `[[BoundTargetFunction]]` 递归，而不是读绑定函数的
`prototype`（绑定函数没有自己的 `prototype` 属性）。与 §7.3.21 step 2 一致。

## 后果

- 优点：`instanceof` 对全部内建族（Error 子类、Array、包装对象）与自建构造函数
  天然可用；`@@hasInstance` 支持自定义协议；绑定函数语义正确。
- 缺点：原型链遍历是 O(depth)；`@@hasInstance` 每次都要做一次属性查找（规范如此）。
- 注意：`instanceof` 对原始值恒为 false（step 3），只有包装对象才可能为 true。
