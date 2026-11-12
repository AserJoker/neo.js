# ADR-056: 左值表达式骨架 — Member / Call / New

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-055（Parser 与 AST）

## 背景

在字面量与标识符之上，先搭表达式骨架中的左值部分：成员访问、计算成员、
函数调用与 `new`。数组、对象、一元/二元/赋值/条件表达式与各类语句延后，
避免一次引入过多节点与优先级规则。

## 决策

### 1. 节点

```text
MemberExpression { object, property, computed }
CallExpression   { callee, arguments }
NewExpression    { callee, arguments }
```

- `MemberExpression` 用 `computed` 区分 `obj.prop` 与 `obj[expr]`。
- `CallExpression` / `NewExpression` 的 `arguments` 为 `NodeList`，暂不含
  SpreadElement。
- 属性名复用 `Identifier` 节点；`obj.default` 等保留字属性名合法。

### 2. 解析层次

```text
parseExpression
  → parseLeftHandSideExpression
      → parseNewExpression        （当前 token 是 new）
      → parseCallExpression
          → parseMemberExpression （primary + 成员后缀）
          → parseCallSuffix       （调用 + 成员后缀循环）
```

- `parseMemberExpression`：`primary` 后循环 `.name` / `[expr]`。
- `parseCallSuffix`：循环 `(args)` / `.name` / `[expr]`，因此
  `a.b(c).d[e]` 自然形成左结合链。
- `parseNewExpression`：`new` 后递归解析 `new` 或 `MemberExpression`，
  可选 `Arguments`，再交给 `parseCallSuffix` 处理 `new Foo().bar()`。

### 3. new 的歧义处理

- `new Foo()`：callee 为 `Foo`，arguments 为空。
- `new Foo`：无括号，arguments 为空。
- `new Foo.bar()`：callee 为 `Foo.bar`（成员），不是 `new Foo` 后 `.bar()`。
- `new Foo().bar()`：`new` 表达式先完成，再应用 `.bar()` 后缀。
- `new new Foo()`：`new` 后递归 `parseNewExpression`。

### 4. 参数列表

- `parseArguments` 接受空列表、逗号分隔表达式与尾随逗号。
- 参数暂按 `parseExpression` 解析；SpreadElement 延后。

## 暂不实现

- 可选链 `?.` 与 `ChainExpression`（词法器已能产出 `QuestionDot`）。
- `new.target`（`MetaProperty`）。
- `super()` / `super.prop`、`import()`。
- 私名字段 `obj.#x`（`PrivateIdentifier`）。
- 数组/对象字面量、一元/二元/赋值/条件表达式、各类语句。

## 后果

- 表达式入口已能覆盖 `a.b[c](d).e`、`new Foo.bar()` 等常见链式形态。
- 后续加入一元/二元/赋值层时，只需在 `parseExpression` 中包裹
  `parseLeftHandSideExpression`，左值层不再改动。
- 可选链与 `new.target` 作为独立节点后续补入，不影响现有结构。
