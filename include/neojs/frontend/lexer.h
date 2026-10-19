#ifndef NEOJS_FRONTEND_LEXER_H
#define NEOJS_FRONTEND_LEXER_H

/*
 * 前端词法器（M1.1 骨架：最小子集，随 M1.2-M1.6 扩充）。
 *
 * 核心设计（docs/milestones.md M1.1）：
 *   - 协同模式：Lexer 不独立产完整个 token 流，而是被 Parser 按需拉取
 *     （next() 推进、peek() 前瞻）。
 *   - Lexer 携带上下文：Parser 在解析表达式时显式传入当前是**读值**还是
 *     **读操作**（LexContext），供 `/` 等歧义符号判定（除法 vs 正则）。
 *   - 注释是**多字符符号**：词法层贪婪匹配 `//` 与块注释起始符，命中即注释，
 *     否则回退为单 `/`（不需上下文）。
 *
 * 符号处理（表驱动贪婪匹配）：所有符号集中为一张表，按长度分组
 * （三字符 → 二字符 → 单字符）。读取时从最长组开始逐字符比对，命中即消费，
 * 自然保证**最长匹配**正确性（如 `>>>=` 不会误切成 `> > >=`）。
 *
 * Token 类型收敛：符号统一用 `TokenKind::Symbol`，具体符号由 `Token::text`
 * 携带（如 `text == "=>"`）。随 M1.2 扩充符号表即可，不再为每个符号建枚举，
 * 避免类型数量爆炸。
 *
 * M1.1 只覆盖最小 token 集：数字字面量、标识符、`+ - * / % ( ) ;`、
 * EOF、以及行/块注释；M1.2 再扩充全部符号。
 */

#include <cstdint>
#include <string>

#include "neojs/frontend/ast.h"
#include "neojs/frontend/diagnostics.h"

namespace neojs {

/* token 类型（M1.1 收敛为少量类别；符号细节由 Token::text 携带） */
enum class TokenKind : uint8_t {
  EndOfFile,  /* 源码结束 */
  Number,     /* 数字字面量（M1.1：十进制 double） */
  Identifier, /* 标识符 */
  Symbol,     /* 符号/标点/运算符：具体内容见 Token::text */
  Error,      /* 词法错误（sink 已记录） */
};

/* Parser 传入的上下文：当前解析位置是读值还是读操作。
 * 供 `/` 判定：读值位置 → 正则扫描；读操作位置 → 除法（M1.3 展开）。 */
enum class LexContext : uint8_t {
  Value,   /* 期待一个值（表达式左值位置）：`/` 在此进入正则 */
  Operator /* 期待一个操作符：`/` 是除法 */
};

/* 单个 token（含精确位置与原始文本） */
struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  SourceLocation start; /* token 起点（1-based line/col + 0-based offset） */
  SourceLocation end;   /* token 终点（最后一个字符的位置） */
  std::string text;     /* 原始文本：Symbol 时携带具体符号（如 "+"、"=>"） */
  double number = 0.0;  /* Number：数值（M1.1） */
};

/* 词法器：被 Parser 按需驱动（next/peek），携带上下文 */
class Lexer {
 public:
  /* source 生命周期归调用方（Parser/CLI），Lexer 不持有拷贝。 */
  Lexer(const char *source, size_t source_len, DiagnosticSink &sink);

  /* 拉取下一个 token（跳过空白与注释），成为当前 token */
  const Token &next();

  /* 当前 token（最近一次 next 的结果） */
  const Token &current() const { return current_; }

  /* 前瞻一个 token（不推进）；M1.1 供 Parser peek 使用 */
  const Token &peek();

  /* 设置/读取上下文（Parser 在解析表达式时显式设置） */
  void setContext(LexContext c) { context_ = c; }
  LexContext context() const { return context_; }

 private:
  /* 字符级推进：pos_ 指向下一个未消费字符；维护 line/col/offset */
  int advance();           /* 返回当前字符并前进；EOF 返回 -1 */
  int currentChar() const; /* 不推进地查看当前字符；EOF 返回 -1 */
  void skipTrivia();       /* 跳过空白与注释（贪婪匹配 // 与块注释起始符） */
  void scanNumber();
  void scanIdentifier();
  void scanSymbol();       /* 表驱动：按长度分组贪婪匹配符号 */
  void scanNext();         /* 扫描下一个 token 到 current_（推进扫描状态） */

  const char *source_;
  size_t source_len_;
  size_t pos_ = 0;      /* 0-based 字节偏移 */
  size_t line_ = 1;     /* 1-based */
  size_t col_ = 1;      /* 1-based */
  LexContext context_ = LexContext::Value;
  DiagnosticSink &sink_;
  Token current_;
  Token peeked_;        /* 前瞻缓存（peek 结果） */
  bool has_peeked_ = false;
};

}  // namespace neojs

#endif  // NEOJS_FRONTEND_LEXER_H
