# ADR-055: Parser 框架与 AST 基类

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（前端架构）、ADR-025（Sema 两遍 walk）、ADR-054（词法器）

## 背景

词法器已能按 `LexicalContext` 拉取 token。下一步需要 Parser 与 AST，但
ECMAScript 语法规模大，一次性铺开全部节点会掩盖设计问题、难以评审。
因此先落**基类与框架**，再按语法族逐个节点实现。

## 决策

### 1. AST 所有权：unique_ptr 独占

```cpp
class Node {
public:
    virtual ~Node() = default;
    NodeKind kind() const;
    SourceRange range() const;
};

using NodePtr = std::unique_ptr<Node>;
using NodeList = std::vector<NodePtr>;
```

- 父节点独占子节点，生命周期清晰，Sema 可直接在节点上挂标注字段。
- `NodeList` 允许空指针，供后续数组 elision 等场景使用。
- 向下转型以 `kind()` 判别后 `static_cast`，不依赖 RTTI。
- 暂不使用 arena：当前优先正确性与可读性；若后续性能测量表明节点分配
  成为瓶颈，再在保持 `NodePtr` 接口的前提下替换分配器。

### 2. 节点按语法族增量加入

- `NodeKind` 只枚举已实现的节点，不预先铺满 ECMAScript 全部语法。
- 每加一个节点：加 `NodeKind`、加节点结构体、加对应 `parseXxx`、加单测。
- **一个节点一个 parse 函数**：`parseNumericLiteral` / `parseStringLiteral` /
  `parseIdentifier` 等只负责该节点的语法与构造；`parseStatement` /
  `parsePrimaryExpression` 只做 token → parse 函数的路由，不构造节点。
- 首批节点：`Program`、`EmptyStatement`、`ExpressionStatement`、
  `Identifier`、`NumericLiteral`、`BigIntLiteral`、`StringLiteral`、
  `BooleanLiteral`、`NullLiteral`、`RegExpLiteral`。
- 语句、表达式、模式、class、module 的其余节点后续逐步加入。

### 3. Parser 持有 Lexer，按需拉取

```cpp
class Parser {
    Lexer lexer_;
    Token current_;
    LexerState currentStart_;   // current_ 扫描前的位置
    SourceLocation previousEnd_; // 上一个已消费 token 的 end
};
```

- `advance(goal)`：记录 `previousEnd_`、保存扫描前状态、按 goal 取下一个 token。
- `peek(goal)`：保存/恢复 Lexer 状态，不移动 `current_`。
- `rescan(goal)`：回到 `currentStart_` 用新 goal 重扫当前 token。用于 `/`
  从除法切换为正则。
- 词法目标由语法位置决定：表达式起始位置用 `RegExp`，运算符位置用 `Div`，
  模板替换表达式结束后用 `TemplateContinuation`。

### 4. 错误收集与恢复

```cpp
struct ParseError { SourceLocation location; std::string message; };
struct ParseResult { NodePtr program; std::vector<ParseError> errors; };
```

- 解析函数失败返回 `nullptr` 并记录错误，不抛异常。
- `parseProgram` 在语句失败后 `synchronize()` 到 `;` / `}` / 输入末尾继续，
  尽量收集多个错误。
- 即使有错误也返回 `Program`，便于诊断工具消费部分语法树。

### 5. 严格模式

- `Parser::strict_` 由指令序言（`"use strict"`）设置，影响后续 token 的
  词法错误（legacy octal、`\8`/`\9`）。
- 检测到指令后立即 `rescan` 当前 token，避免下一个 token 仍按旧严格性扫描。

## 后果

- 框架可独立测试：空程序、trivia、错误收集、源范围。
- 后续每个节点只改 `Node.h`、`Parser.h/.cpp` 与对应测试，评审范围小。
- 正则/模板等上下文相关切词由 `rescan` 与 goal 参数承载，不需要回退词法器。
