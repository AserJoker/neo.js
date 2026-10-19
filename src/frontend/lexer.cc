#include "neojs/frontend/lexer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace neojs {

namespace {

/* 数字字面量解析（M1.1：十进制 double；M1.2 扩充进制/科学计数/分隔符） */
bool parseDecimal(const char *s, size_t len, double &out) {
  if (len == 0) return false;
  std::string tmp(s, len);
  char *end = nullptr;
  out = std::strtod(tmp.c_str(), &end);
  return end == tmp.c_str() + len;
}

/* -------------------------------------------------------------------------- */
/* 符号表（表驱动贪婪匹配）                                                   */
/*                                                                              */
/* 所有符号集中在此，按长度分组，从长到短（四字符 → 三字符 → 二字符 → 单    */
/* 字符）读取时逐字符比对，命中即消费，自然保证**最长匹配**正确性            */
/* （如 `>>>=` 不会误切为 `>>=` + `>`）。                                     */
/*                                                                              */
/* M1.1 只列最小子集；M1.2 扩充时在此追加即可，无需新增 Token 类型。          */
/* -------------------------------------------------------------------------- */

/* 四字符符号 */
constexpr const char *kSymbols4[] = {
    ">>>=", /* 无符号右移赋值（M1.4） */
};

/* 三字符符号 */
constexpr const char *kSymbols3[] = {
    "...", /* 展开（M1.4） */
    ">>>", /* 无符号右移（M1.4） */
    "===", /* 严格相等（M1.4） */
    "!==", /* 严格不等（M1.4） */
    "&&=", /* 逻辑与赋值（M1.4） */
    "||=", /* 逻辑或赋值（M1.4） */
    "??=", /* 空值赋值（M1.4） */
    "**=", /* 幂赋值（M1.4） */
    "<<=", /* 左移赋值（M1.4） */
    ">>=", /* 右移赋值（M1.4） */
};

/* 二字符符号 */
constexpr const char *kSymbols2[] = {
    "=>", /* 箭头函数（M1.4） */
    "==", /* 相等（M1.4） */
    "!=", /* 不等（M1.4） */
    "<=", /* 小于等于（M1.4） */
    ">=", /* 大于等于（M1.4） */
    "&&", /* 逻辑与（M1.4） */
    "||", /* 逻辑或（M1.4） */
    "??", /* 空值合并（M1.4） */
    "**", /* 幂（M1.4） */
    "<<", /* 左移（M1.4） */
    ">>", /* 右移（M1.4） */
    "+=", /* 复合赋值（M1.4） */
    "-=", /* 复合赋值（M1.4） */
    "*=", /* 复合赋值（M1.4） */
    "/=", /* 复合赋值（M1.4） */
    "%=", /* 复合赋值（M1.4） */
    "&=", /* 位与赋值（M1.4） */
    "|=", /* 位或赋值（M1.4） */
    "^=", /* 位异或赋值（M1.4） */
    "++", /* 自增（M1.4） */
    "--", /* 自减（M1.4） */
    "?.", /* 可选链（M1.4） */
};

/* 单字符符号 */
constexpr const char *kSymbols1[] = {
    "+", "-", "*", "/", "%", /* 算术 */
    "(", ")", "{", "}", "[", "]", /* 括号 */
    ";", ",", ".",                 /* 分隔 */
    "<", ">", "=", "!",           /* 比较/赋值 */
    "&", "|", "^", "~",           /* 位运算 */
    "?", ":",                       /* 三元 */
    "#",                             /* 私有字段（M1.4） */
    "`",                             /* 模板串（M1.3） */
    "'", "\"",                     /* 字符串引号（M1.2） */
    "$",                             /* 模板插值（M1.3） */
    "\\",                           /* 反斜杠（转义，M1.2） */
};

/* 按长度分组查找：从长到短，命中第一个即返回。 */
template <size_t N>
bool matchSymbolGroup(const char *const (&group)[N], const char *src, size_t len,
                      size_t symbol_len, size_t &match) {
  for (size_t i = 0; i < N; ++i) {
    if (len >= symbol_len && std::memcmp(src, group[i], symbol_len) == 0) {
      match = i;
      return true;
    }
  }
  return false;
}

}  // namespace

Lexer::Lexer(const char *source, size_t source_len, DiagnosticSink &sink)
    : source_(source ? source : ""), source_len_(source ? source_len : 0), sink_(sink) {}

int Lexer::currentChar() const {
  if (pos_ >= source_len_) return -1;
  return static_cast<unsigned char>(source_[pos_]);
}

int Lexer::advance() {
  if (pos_ >= source_len_) return -1;
  int c = static_cast<unsigned char>(source_[pos_]);
  ++pos_;
  if (c == '\n') {
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  return c;
}

/* 跳过空白与注释。注释是**多字符符号**：贪婪匹配 // 与块注释起始符；
 * 命中失败（单个 /）不是注释，由调用方当作除法/正则处理（M1.3）。 */
void Lexer::skipTrivia() {
  for (;;) {
    int c = currentChar();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
      advance();
      continue;
    }
    if (c == '/' && pos_ + 1 < source_len_) {
      int n = static_cast<unsigned char>(source_[pos_ + 1]);
      if (n == '/') {
        /* 行注释：到行尾 */
        advance();
        advance();
        while (currentChar() != -1 && currentChar() != '\n') advance();
        continue;
      }
      if (n == '*') {
        /* 块注释：读到闭合符；未闭合发错误 */
        advance();
        advance();
        bool closed = false;
        while (currentChar() != -1) {
          if (currentChar() == '*' && pos_ + 1 < source_len_ &&
              static_cast<unsigned char>(source_[pos_ + 1]) == '/') {
            advance();
            advance();
            closed = true;
            break;
          }
          advance();
        }
        if (!closed) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated block comment",
                      {line_, col_, pos_}});
        }
        continue;
      }
      /* 单个 /：不是注释，停止跳过（交给 token 扫描） */
    }
    break;
  }
}

void Lexer::scanNumber() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::Number;
  const size_t num_start = pos_;
  while (currentChar() != -1 &&
         (std::isdigit(static_cast<unsigned char>(currentChar())) || currentChar() == '.')) {
    advance();
  }
  t.text = std::string(source_ + num_start, pos_ - num_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  if (!parseDecimal(source_ + num_start, pos_ - num_start, t.number)) {
    t.kind = TokenKind::Error;
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid number literal", t.start});
  }
  current_ = std::move(t);
}

void Lexer::scanIdentifier() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::Identifier;
  const size_t id_start = pos_;
  while (currentChar() != -1 &&
         (std::isalnum(static_cast<unsigned char>(currentChar())) || currentChar() == '_')) {
    advance();
  }
  t.text = std::string(source_ + id_start, pos_ - id_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  current_ = std::move(t);
}

/* 表驱动贪婪匹配符号：四字符 → 三字符 → 二字符 → 单字符。命中即消费。 */
void Lexer::scanSymbol() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::Symbol;

  const char *src = source_ + pos_;
  const size_t remain = source_len_ - pos_;
  size_t match = 0;
  size_t width = 0;

  if (matchSymbolGroup(kSymbols4, src, remain, 4, match)) {
    width = 4;
  } else if (matchSymbolGroup(kSymbols3, src, remain, 3, match)) {
    width = 3;
  } else if (matchSymbolGroup(kSymbols2, src, remain, 2, match)) {
    width = 2;
  } else if (matchSymbolGroup(kSymbols1, src, remain, 1, match)) {
    width = 1;
  } else {
    /* 未知字符：消费并报错 */
    int c = advance();
    t.kind = TokenKind::Error;
    t.text = (c != -1) ? std::string(1, static_cast<char>(c)) : "";
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected character", t.start});
    t.end = {line_, col_ - 1, pos_ - 1};
    current_ = std::move(t);
    return;
  }

  for (size_t i = 0; i < width; ++i) advance();
  switch (width) {
    case 4:
      t.text = kSymbols4[match];
      break;
    case 3:
      t.text = kSymbols3[match];
      break;
    case 2:
      t.text = kSymbols2[match];
      break;
    default:
      t.text = kSymbols1[match];
      break;
  }

  t.end = {line_, col_ - 1, pos_ - 1};
  current_ = std::move(t);
}

/* 扫描下一个 token 到 current_（推进扫描状态；next/peek 复用） */
void Lexer::scanNext() {
  skipTrivia();
  Token t;
  t.start = {line_, col_, pos_};

  int c = currentChar();
  if (c == -1) {
    t.kind = TokenKind::EndOfFile;
    t.end = t.start;
    current_ = std::move(t);
    return;
  }

  if (std::isdigit(static_cast<unsigned char>(c))) {
    scanNumber();
    return;
  }
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    scanIdentifier();
    return;
  }

  /* 其余一律走符号表（贪婪最长匹配） */
  scanSymbol();
}

/* 拉取下一个 token（推进扫描状态），成为当前 token */
const Token &Lexer::next() {
  if (has_peeked_) {
    /* 有缓存的前瞻结果：直接提升为当前（扫描位置本就已越过它） */
    current_ = std::move(peeked_);
    has_peeked_ = false;
    return current_;
  }
  scanNext();
  return current_;
}

const Token &Lexer::peek() {
  if (!has_peeked_) {
    /* 保存当前，扫描下一个，再恢复 current 与扫描状态：peek 不改状态 */
    Token saved = current_;
    const size_t saved_pos = pos_;
    const size_t saved_line = line_;
    const size_t saved_col = col_;
    scanNext();
    peeked_ = current_;
    current_ = std::move(saved);
    pos_ = saved_pos;
    line_ = saved_line;
    col_ = saved_col;
    has_peeked_ = true;
  }
  return peeked_;
}

}  // namespace neojs
