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

### 4. 运行期严格性标注（执行模型）

Sema 只负责早期错误；运行期需要严格性判定的操作（未声明赋值、只读属性写、
`with` 环境、`delete` 等）读取当前作用域链上的 `"strict"` 标注。标注来源：

```text
GlobalScope            → 不标注（脚本默认 sloppy）
Script 执行            → VM 入口建临时脚本作用域，仅 code.strict 时标注
ModuleScope            → 恒标注 strict（§16.2.1.1）
Class 方法/字段初始化   → set_strict 标注函数闭包作用域
Function               → 自身指令序言或外围严格代码（Sema 判定后发射 set_strict）
直接 eval              → 继承调用方
严格 eval              → eval 词法作用域发射 set_scope_strict
```

脚本临时作用域只承载执行模式：顶层 `let/const` 仍经 `define_global_lexical`
写入 GlobalScope 声明记录，`var`/函数声明仍落 global object 属性，`this`
仍由 `currentFunctionEnv_ = global_` 解析。因此严格性标注不会改变全局绑定归属。

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
- 运行期不再把 GlobalScope 默认标为 strict；脚本、模块、函数、eval 各自
  按规范来源标注，脚本默认 sloppy。
- 代价是 Token/字面量节点需要携带 legacy 形态标记。
