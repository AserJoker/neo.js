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
| BinaryExpression（+ - * / < <= > >=） | 左、右 + 对应指令 |

变量声明、函数、调用、对象、异常、模块等节点暂记录 unsupported 错误，
在后续切片中逐个补齐；每补一个节点，同时补 Compiler 测试与 VM 端到端测试。

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
- 缺点：初始子集不含变量提升/TDZ 预扫描、函数与模块，真实程序还不能编译；
  这些需要 Compiler 自己的作用域/函数上下文，在后续切片引入。
- 待定：函数体编译上下文（return 目标、闭包捕获发射）、模块三段式、
  常量池去重、SourceMap 对多字节指令的精确边界。
