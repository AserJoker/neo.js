#include "neojs/frontend/lexer.h"

#include <cctype>
#include <cstdlib>
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

  switch (c) {
    case '+':
      advance();
      t.kind = TokenKind::Plus;
      break;
    case '-':
      advance();
      t.kind = TokenKind::Minus;
      break;
    case '*':
      advance();
      t.kind = TokenKind::Star;
      break;
    case '/':
      /* 单个 /：除法 or 正则（M1.3 按 context 判定）。M1.1 统一作 Slash，
       * 但验证 context 已传入（见 parser.cc 的注释）。 */
      advance();
      t.kind = TokenKind::Slash;
      break;
    case '%':
      advance();
      t.kind = TokenKind::Percent;
      break;
    case '(':
      advance();
      t.kind = TokenKind::LParen;
      break;
    case ')':
      advance();
      t.kind = TokenKind::RParen;
      break;
    case ';':
      advance();
      t.kind = TokenKind::Semicolon;
      break;
    default:
      if (std::isdigit(static_cast<unsigned char>(c))) {
        scanNumber();
        return;
      }
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        scanIdentifier();
        return;
      }
      advance();
      t.kind = TokenKind::Error;
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected character", t.start});
      break;
  }

  t.end = {line_, col_ - 1, pos_ - 1};
  if (c != -1) t.text = std::string(1, static_cast<char>(c));
  current_ = std::move(t);
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
