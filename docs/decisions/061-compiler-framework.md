# ADR-061: Compiler 框架 — AST + Sema 标注 → Code

- 状态: 提议中（框架设计）
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-024（字节码指令集）、ADR-055（Parser 与 AST）、
  ADR-058（Sema Walker 与标注）、ADR-060（VM 框架）

## 背景

Parser 产出 AST，Sema 在 AST 上完成作用域/解析/早期错误并输出
`AnnotationStore`。现在需要 Compiler 把这两份产物翻译成 VM 可执行的
`vm::Code`，形成 `AST → Sema → Compiler → VM → Engine` 的完整链路。

## 决策

### 1. 结构对齐 Parser

```cpp
struct CompilerOptions {
    std::string filename = "<anonymous>";  // SourceMap 用
};

struct CompileError {
    frontend::SourceRange range;
    std::string message;
};

struct CompileResult {
    // Code 含扁平字节码/常量表等大数组，按值传递会整块复制。
    // 由 Module 唯一持有；VM / script Function 只借用 const Code*。
    std::unique_ptr<vm::Code> code;
    std::vector<CompileError> errors;
    bool ok() const { return code != nullptr && errors.empty(); }
};

class Compiler {
public:
    Compiler(const frontend::Program& program,
             const sema::SemaResult& sema,
             CompilerOptions options = {});
    CompileResult compile();
};
```

- 输入是 **AST + SemaResult**：AST 提供语法结构，`SemaResult.annotations`
  提供 Compiler 无法从语法单独判定的信息（函数种类、闭包捕获、模块绑定等）。
- 输出 `std::unique_ptr<vm::Code>` 与错误列表，形态与 `ParseResult` 一致。
  `Code` 含大块扁平数组，**不按值传递**：Module 唯一持有，VM / script Function
  只借用 `const Code*`。
- **一个节点一个 compile 函数**；`compileStatement` / `compileExpression`
  只做 `NodeKind → 具体函数` 的路由，不发射指令。
- 前置条件 `sema.ok()`；不满足时 Compiler 直接返回错误，不在错误 AST 上
  继续生成字节码。

### 2. 错误处理

- 未支持的节点/运算符记录 `CompileError`（带节点 `SourceRange`），不静默
  生成语义错误的字节码。
- 未支持的表达式仍发射 `push_undefined`，维持「表达式留下一个值」的栈契约，
  便于后续指令继续被诊断；但 `CompileResult::ok()` 为 false，调用方不应执行。

### 3. SourceMap

- 每个 `compileStatement` / `compileExpression` 记录本节点发射的字节范围
  `[begin, code_.here())`，位置取 `node.range().start` 与
  `CompilerOptions::filename`。
- 嵌套表达式各自登记，VM 反查时取最内层（ADR-060 §3.1）。

### 4. 初始纵向切片

框架先打通 VM 已支持的 V0/V1 子集：

| AST 节点 | 字节码 |
|----------|--------|
| Program | 逐条语句 + `halt` |
| EmptyStatement | 无 |
| ExpressionStatement | 表达式 + `pop` |
| ReturnStatement | 表达式（或 `push_undefined`）+ `ret` |
| BlockStatement | `push_scope` + 语句 + `pop_scope` |
| Numeric/String/Boolean/NullLiteral | `push_*` |
| Identifier | `load` |
| VariableDeclaration | 声明点 `store`（var）/ `initialize`（let/const） |
| AssignmentExpression | `=`：右值 + `dup` + `store`；复合：`load` + 右值 + 运算 + `dup` + `store` |
| UpdateExpression | 前缀：`load` + 1 + 运算 + `dup` + `store`；后缀：`load` + `clone` + `swap` + 1 + 运算 + `store` |
| BinaryExpression（+ - * / < <= > >=） | 左、右 + 对应指令 |
| FunctionDeclaration/Expression | 跳过函数体 + 函数体 + `push_function` + `set_func_name` + `set_closure` + `define`（声明） |
| ArrowFunctionExpression | 同上，另加 `load_this` + `bind_this`，无 prototype/不可构造 |
| CallExpression（非成员） | callee + args + `call N` |
| CallExpression（成员） | obj + `dup` + key + `get_prop` + args + `call_method N` |
| MemberExpression | obj + key + `get_prop` |
| 成员赋值（`=`） | obj + key + RHS + `set_prop` |
| ObjectExpression | `new_object` + 每个属性 `dup; key; value; define_prop; pop` |
| ArrayExpression | `new_array N` + 每个元素 `dup; index; value; define_prop; pop` |
| ThisExpression | `load_this` |
| YieldExpression | 参数（或 `push_undefined`）+ `yield`；`yield*` 暂 unsupported |
| FunctionDeclaration/Expression（`function*`） | `push_function` 带 `kFunctionGenerator`，不可构造、有 prototype |
| IfStatement | test + `jfalse` else；then；有 else 时 `jmp end` + 回填 |
| WhileStatement | `push_label`；loop：test + `jfalse exit`；body；`jmp loop`；exit：`pop_label` |
| DoWhileStatement | `push_label`；loop：body；test + `jtrue loop`；exit：`pop_label` |
| LabeledStatement | 标签传给循环；非循环标签单独 `push_label`/`pop_label` |
| BreakStatement | `break "label"`（空串 = 最近可 break 目标） |
| ContinueStatement | `continue "label"`（空串 = 最近循环） |
| ThrowStatement | 表达式 + `throw` |
| TryStatement | `try_begin` + try 块 + `try_body_end`；catch/finally 独立块 + 各自 end |
| ImportDeclaration | 无字节码；产出 `ModuleImportBinding`（link 期绑定槽位） |
| ExportNamedDeclaration | 编译内层声明；产出 `ModuleExportBinding`（本地 / `from` re-export） |
| ExportDefaultDeclaration | 具名函数导出其绑定；匿名函数/表达式存入隐藏槽位 `*default*` |
| ExportAllDeclaration | 无字节码；产出 `exportAll` 或 `export * as ns` 元数据 |

声明提升：作用域入口先建槽——`var`（递归收集，不穿透函数）→
`push_undefined; define`；直接 `let/const` → `push_uninitialized; define(mutable,
initialized=false)`；直接函数声明先建 undefined 槽，再创建函数并 `store`
（分两步保证函数间可互相捕获）。声明点只做 `store` / `initialize`。

函数体序言：形参 `push_undefined; define` → `store_this` → 按序 `store` 实参 →
`push_scope` → 函数体 → `push_undefined; ret`。

展开语法、对象访问器/`__proto__`、可选链、复合成员赋值、`export class` 等节点
暂记录 unsupported 错误，
在后续切片中逐个补齐；每补一个节点，同时补 Compiler 测试与 VM 端到端测试。

**模块**：`Program.sourceType == Module` 时按三段式发射——`declarePc`（符号
定义区）、`instantiatePc`（函数定义区）、`evaluatePc`（源码逻辑入口），
三段各自以 `halt` 结束；import/export 声明不产生运行期字节码，只产出
`CompileResult::imports/exports` 元数据，由 Context 在 link 阶段消费。

**break / continue 已接入 label 栈**（ADR-060 §7）：循环/switch/带标签语句
成对发射 `push_label`/`pop_label`；跨 finally 的展开留给 try 组实现时处理。

### 5. 文件布局

```
src/private/compiler/
  Compiler.h      // 输入/输出、选项、分派与 compile 函数声明
  Compiler.cpp    // 分派与各节点字节码发射
tests/unit/compiler/
  CompilerTest.cpp
```

## 后果

- 优点：Compiler 与 Parser 同构，节点覆盖度可逐条推进；错误显式；SourceMap
  与 VM 的异常定位直接对接；后续只需在分派表里补节点。
- 缺点：尚未覆盖 for/switch 与模块，真实程序还不能完整编译；
  函数声明提升已按「先建槽 → 再创建函数 → store」实现，但尚未独立函数定义区。
- 待定：模块绑定、常量池去重、SourceMap 对多字节指令的精确边界。
