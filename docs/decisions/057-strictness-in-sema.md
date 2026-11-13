# ADR-057: 严格模式不进入 Lexer/Parser

- 状态: 已接受 (2026-11-13)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-054（词法器）、ADR-055（Parser 与 AST）

## 背景

早期实现让 Lexer 接收 `strict` 标志，Parser 维护 `strict_` 并在遇到
`"use strict"` 后切换词法目标。这带来两个问题：

- 严格性是**作用域语义**，不是词法/语法形状；Lexer/Parser 不应感知。
- 解析中途切换严格性需要 `rescan`，且报错时语法树可能不完整。

## 决策

### 1. Lexer 只扫描 sloppy 超集

- `LexicalContext` 只保留词法目标（Div / RegExp / TemplateContinuation）。
- 严格模式专属的非法形态照常扫描，并在 `Token` 上记录：
  - `legacyLeadingZero`（`010` / `08` / `09`）
  - `legacyOctalEscape`（`\1` ... `\7`）
  - `legacyEightOrNine`（`\8` / `\9`）
- 模板字面量中的八进制转义与严格性无关，仍在 Lexer 当场报错。

### 2. Parser 只做 text → AST

- 删除 `Parser::strict_`，不再因严格性拒绝任何语法。
- `"use strict"` 只体现为 `Directive` 节点；指令序言由
  `parseStatementListItem` 识别，供 Sema 使用。
- `with`、`delete identifier`、重复参数等一律正常成树。
- `Program` 不再保存 `strict` 字段。
- 函数体/class 体结束后，由调用方按声明或表达式位置选择
  `rescan` 目标（RegExp / Div），与严格性无关。

### 3. Sema 负责严格性

Sema 遍历 AST 时维护严格性栈：

```text
Program(script)        → sloppy
Program(module)        → strict
Function/Arrow/Method  → 继承外层 + 自身 directive prologue
ClassBody              → 恒 strict
```

据此检查：

- legacy leading zero / octal escape / `\8` `\9`
- `with`
- `delete identifier`
- 重复参数
- `eval` / `arguments` 作为绑定名或赋值目标
- 严格模式保留字
- 非简单参数 + `"use strict"`
- 块级函数等 Annex B 差异

## 不推迟的内容

以下属于语法形状，仍由 Parser 决定：

- ASI、cover grammar、解构/字面量歧义
- 上下文关键字：`async`、`let`、`of`、`get`/`set`、`static`
- generator/async 上下文中的 `yield` / `await`
- 模板续段、正则/除法目标

## 后果

- Lexer/Parser 不再有严格性状态与相关 `rescan`。
- 报错时 AST 完整，Sema 能给出带作用域上下文的语义化诊断。
- 严格性作用域与继承关系集中在一处维护。
- 代价是 Token/字面量节点需要携带 legacy 形态标记。
