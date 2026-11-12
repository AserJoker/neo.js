# ADR-054: 词法器 — 拉取式扫描与词法目标

- 状态: 已接受 (2026-11-12)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-052（前端源流）、ADR-053（Unicode 分类）

## 背景

JavaScript 的切词依赖语法上下文，无法像 C 一样先一次性生成 token 流：

1. `/` 可能是除法，也可能是正则字面量。仅看前一个 token 不够：
   `if (x) /re/.test(y)` 的前一个 token 是 `)`，但此处是正则。
2. 模板字面量中的 `}`：可能是普通右花括号，也可能是模板续段
   （`TemplateMiddle` / `TemplateTail`）的开始，只有 parser 知道当前是否
   处在模板替换表达式末尾。
3. 严格模式影响 legacy octal、`\8`/`\9` 等词法错误。

因此 parser 必须先决定“下一个 token 按什么词法目标扫描”，再向词法器拉取。

## 决策

### 1. 拉取式接口

```cpp
Lexer lexer(source);
Token token = lexer.next(context);   // 按需扫一个 token
```

- 不预生成 token 流，内存与源码同阶，且天然支持上下文相关切词。
- `Lexer::save()` / `restore()` 保存 `CharStream` 游标，供 parser 做语法层
  前瞻（例如 `async function`、箭头函数参数）。
- parser 不直接 `CharStream::peek` 判断单词类型；字符级分类（标识符、数字、
  字符串、转义）全部留在词法器内，避免重复实现。

### 2. 词法上下文

```cpp
enum class LexicalGoal {
    Div,                  // 表达式位置：/ 是除法
    RegExp,               // 表达式起始：/ 是正则
    TemplateContinuation  // 模板中：当前 } 继续扫模板 chunk
};

struct LexicalContext {
    LexicalGoal goal = LexicalGoal::Div;
    bool strict = false;
};
```

parser 在表达式起始位置传 `RegExp`，其余位置传 `Div`；模板替换表达式结束后
传 `TemplateContinuation`，词法器消费 `}` 并返回 `TemplateMiddle` 或
`TemplateTail`。

### 3. Token 表示

- `lexeme` 是源码切片（`string_view`），生命周期随源缓冲；
- `text` 是解码后的值：标识符名字、字符串 cooked 值、模板 cooked 值、
  正则 pattern、BigInt 数字串；
- `numberValue` / `numericRadix` / `regexFlags` 承载字面量附加信息；
- `precededByLineTerminator` 供 ASI 与 `++`/`--` 限制使用；
- 固定保留字直接产出关键字 token；`let` / `async` / `await` / `yield` /
  `static` 等上下文关键字仍产出 `Identifier`，由 parser 判定。

### 4. 扫描规则要点

- trivia：空白、行终止符、`//`、`/* */`、文件开头 `#!`；块注释内的行终止符
  计入 `precededByLineTerminator`。
- 标识符：`ID_Start` / `ID_Continue`（ADR-053），支持 `\uXXXX` / `\u{...}`，
  转义结果重新走分类谓词。
- 数字：十进制、`0x`/`0o`/`0b`、指数、数字分隔符、BigInt `n`；非十进制与
  legacy octal 通过 `bigint::BigInt::parse` 得到精确值再转 double；legacy
  octal 与 `\8`/`\9` 在严格模式下报错。
- 字符串：简单转义、`\xHH`、`\uXXXX`、`\u{...}`、行延续；legacy octal 在
  严格模式报错；孤立代理按 WTF-8 保留。
- 模板：cooked 值把 CRLF / CR 归一化为 LF；禁止 octal 与 `\8`/`\9`。
- 正则：跟踪字符类 `[...]`，`/` 在类内不结束；flags 按 IdentifierPart 收集；
  flags 的合法性由后续正则编译器校验。
- 标点：最长匹配；`?.` 后跟数字时不合并（`a?.5` 是 `a ? .5`）。
- 扫描失败返回 `TokenKind::Illegal`，`text` 为错误消息，并保证至少前进一个
  码点，避免调用方死循环。

## 后果

- 词法器不持有 parser 状态，可独立单测；parser 通过 `LexicalContext` 驱动。
- 正则 flags 的语义校验、模板 raw 值、错误位置诊断留待 parser / 正则编译器。
- 后续 parser 接入时，只需在表达式起始点与模板续段处正确设置 goal。
