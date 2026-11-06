# ADR-038: Error 基类 — [[ErrorData]] 标记 + 调用栈渲染

- 状态: 已接受 (2026-11-06)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-015（Value meta+data）、ADR-018（对象模型）、ADR-019（赋值语义）、
          ADR-034（原型-构造函数）、ADR-037（调用栈）

## 背景

异常（ADR-017 §3）目前携带的是 String 或任意 Value，没有标准错误对象。
需要实现 ES2024 §20.5 的 Error 体系，先做基类 `Error`。

## 决策

### 1. Error 是「普通对象 + [[ErrorData]] 标记」

- 新增 `TypeKind::Error`，`ErrorType` 声明 `objectLayout = true`。
- `ErrorData` 继承 `ObjectData`，**无自有字段**——`[[ErrorData]]` 是标记性内部槽，
  存在性即标记，用于类型判定（`Object.prototype.toString` 的 tag、后续 `Error.isError`）。
- 属性/原型/访问器/GC 遍历全部复用 `ObjectData`，无需特化。

### 2. 子类共用同一 Type

`TypeError`/`RangeError` 等**共用 `TypeKind::Error`**，差异只在各自的 prototype 与
`name` 属性——与规范一致（§20.5.6 各子类实例都是 `[[ErrorData]]` 对象，靠原型链区分）。

实现的一般性错误子类（ES2024 §20.5.6）：
`TypeError` / `RangeError` / `ReferenceError` / `SyntaxError` / `URIError`。

- **EvalError 已不在 ES2024 规范中，不实现**；
- `AggregateError`（聚合错误）/ `SuppressedError`（抑制错误）不在本阶段范围。

每个子类：原型是**普通对象**（无 `[[ErrorData]]`，§20.5.6.3），原型链指向
`%Error.prototype%`，自有 `name`（writable/不可枚举/可配置）与 `constructor`。
构造函数先 `createError(message, 子类名)` 建实例（stack 首行用子类名），
再把原型改挂到子类原型。

### 3. message / stack 是自有属性，不是 data 字段

两者都由 `createError` 在构造时用 `defineProperty` 定义为
**writable / 不可枚举 / 可配置**（ES2024 §20.5.3.1 / V8 的 stack 描述符）：

- `message`：空串时**不定义**（§20.5.1.1 step 3：undefined 不建属性），
  读取时落到 `Error.prototype.message = ""`。
- `stack`：由调用栈快照渲染（见 §4）。

放属性容器而非 data 字段的好处：可写性/枚举性/delete 语义完全走对象模型，
用户 `err.message = "x"`、`delete err.stack` 都自然正确，无需特化 `setProperty`。

### 4. stack 用 trace(position) 渲染

```cpp
createError(message, name = "Error", position = {}, bindName = {})
```

- **`position` 是出错点**（栈顶帧此刻的位置）。按 ADR-037，只有 VM 知道它，
  故由调用方传入；`{}` 表示未知。
- 渲染用 `trace(position)`：clone 当前调用栈并用 `position` 补全栈顶帧，
  **不改动** Context 的栈。
- 输出格式（与 V8 一致，最近调用在前）：

```
Error: boom
    at bar (a.js:30:1)
    at foo (a.js:20:9)
    at main (a.js:10:5)
```

- 位置未知的帧只输出函数名；`<internal>` 帧输出 `at fn (<internal>)`。
- 首行 `name: message`，message 为空时只有 `name`。

### 5. 原型是普通对象，不带 [[ErrorData]]

`%Error.prototype%` 与各子类原型都是 **ordinary object**（ES2024 §20.5.3 / §20.5.6.3），
**没有** `[[ErrorData]]` 内部槽——用 `createObject` 而非 `createError` 创建。
原型上只有 `name` / `message` / `toString`，没有 `stack`。

## 后果

- 优点：Error 完全复用对象模型，零特化代码；stack 与调用栈基础设施（ADR-037）
  自然衔接；子类只需新原型 + name；message/stack 的属性语义天然正确。
- 缺点：`createError` 多一个 `position` 参数（但这是唯一知道出错点的地方）；
  stack 在创建时快照，后续调用栈变化不影响已创建的 Error（与 V8 一致）。
