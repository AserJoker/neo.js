#include "neojs/frontend/lexer.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include <unicode/uchar.h>
#include <unicode/utf8.h>

namespace neojs {

namespace {

/* -------------------------------------------------------------------------- */
/* Unicode 工具                                                               */
/* -------------------------------------------------------------------------- */

/* LineTerminator（ECMA-262 §12.3）：\n \r U+2028 U+2029（\r\n 计单个） */
bool isLineTerminator(int c) {
  return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}

/* Unicode White_Space（u_isUWhiteSpace），但排除 LineTerminator（另有处理） */
bool isUnicodeWhitespace(int c) {
  return c != -1 && u_isUWhiteSpace(static_cast<UChar32>(c)) && !isLineTerminator(c);
}

/* ID_Start（UCHAR_ID_START）：标识符首字符；含 _ 与 $ */
bool isIdStart(int c) {
  if (c == '_' || c == '$') return true;
  return c != -1 && u_hasBinaryProperty(static_cast<UChar32>(c), UCHAR_ID_START);
}

/* ID_Continue（UCHAR_ID_CONTINUE）：标识符后续字符；含 _ 与 $ 与 ZWNJ/ZWJ */
bool isIdContinue(int c) {
  if (c == '_' || c == '$') return true;
  if (c == 0x200C || c == 0x200D) return true; /* ZWNJ / ZWJ */
  return c != -1 && u_hasBinaryProperty(static_cast<UChar32>(c), UCHAR_ID_CONTINUE);
}

/* 十进制数字（u_isdigit）：JS 数字字面量的十进制部分 */
bool isDecimalDigit(int c) {
  return c != -1 && u_isdigit(static_cast<UChar32>(c));
}

/* 十六进制数字 */
bool isHexDigit(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hexValue(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* 数字字面量解析（M1.2：全进制 + 科学计数 + 分隔符 + BigInt）。
 * 输入 text 已去除非法字符校验，此处负责转换与分隔符位置校验。 */
struct ParsedNumber {
  double value = 0.0;
  bool ok = false;
  bool is_bigint = false;
};

ParsedNumber parseNumber(const std::string &text) {
  ParsedNumber r;
  if (text.empty()) return r;

  bool bigint = text.back() == 'n';
  size_t len = text.size();
  if (bigint) len -= 1;
  std::string body = text.substr(0, len);

  int base = 10;
  size_t start = 0;
  if (body.size() >= 2 && body[0] == '0') {
    char p = body[1];
    if (p == 'b' || p == 'B') { base = 2; start = 2; }
    else if (p == 'o' || p == 'O') { base = 8; start = 2; }
    else if (p == 'x' || p == 'X') { base = 16; start = 2; }
  }

  if (base != 10) {
    /* 非十进制：纯整数，逐位转换，_ 分隔符必须夹在数字间 */
    double value = 0.0;
    bool saw = false;
    bool last_sep = false;
    for (size_t i = start; i < body.size(); ++i) {
      char ch = body[i];
      if (ch == '_') {
        if (!saw || last_sep) return r;
        last_sep = true;
        continue;
      }
      int hv = hexValue(static_cast<unsigned char>(ch));
      if (hv < 0 || hv >= base) return r;
      last_sep = false;
      saw = true;
      value = value * base + hv;
    }
    if (!saw || last_sep) return r;
    r.value = value;
    r.ok = true;
    r.is_bigint = bigint;
    return r;
  }

  /* 十进制：去 _ 分隔符后交给 strtod（含小数/指数），但需先校验分隔符位置 */
  bool saw = false;
  bool last_sep = false;
  for (size_t i = start; i < body.size(); ++i) {
    char ch = body[i];
    if (ch == '_') {
      if (!saw || last_sep) return r;
      last_sep = true;
      continue;
    }
    last_sep = false;
    if (ch >= '0' && ch <= '9') { saw = true; continue; }
    if (ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') continue;
    return r;
  }
  if (!saw || last_sep) return r;

  std::string clean;
  clean.reserve(body.size());
  for (char ch : body) {
    if (ch != '_') clean += ch;
  }
  char *end = nullptr;
  r.value = std::strtod(clean.c_str(), &end);
  r.ok = (end == clean.c_str() + clean.size());
  r.is_bigint = bigint;
  return r;
}

/* -------------------------------------------------------------------------- */
/* 符号表（表驱动贪婪匹配）                                                   */
/* -------------------------------------------------------------------------- */

/* 四字符符号 */
constexpr const char *kSymbols4[] = {
    ">>>=", /* 无符号右移赋值 */
};

/* 三字符符号 */
constexpr const char *kSymbols3[] = {
    "...", /* 展开 */
    ">>>", /* 无符号右移 */
    "===", /* 严格相等 */
    "!==", /* 严格不等 */
    "&&=", /* 逻辑与赋值 */
    "||=", /* 逻辑或赋值 */
    "\?\?=", /* 空值赋值 */
    "**=", /* 幂赋值 */
    "<<=", /* 左移赋值 */
    ">>=", /* 右移赋值 */
    "<!--", /* HTML 风格注释起始（在 skipTrivia 处理，这里保留占位避免误切） */
};

/* 二字符符号 */
constexpr const char *kSymbols2[] = {
    "=>", /* 箭头函数 */
    "==", /* 相等 */
    "!=", /* 不等 */
    "<=", /* 小于等于 */
    ">=", /* 大于等于 */
    "&&", /* 逻辑与 */
    "||", /* 逻辑或 */
    "\?\?", /* 空值合并 */
    "**", /* 幂 */
    "<<", /* 左移 */
    ">>", /* 右移 */
    "+=", "-=", "*=", "/=", "%=", /* 复合赋值 */
    "&=", "|=", "^=",           /* 位复合赋值 */
    "++", "--",                 /* 自增自减 */
    "?.",                       /* 可选链 */
};

/* 单字符符号 */
constexpr const char *kSymbols1[] = {
    "+", "-", "*", "/", "%", /* 算术 */
    "(", ")", "{", "}", "[", "]", /* 括号 */
    ";", ",", ".",                 /* 分隔 */
    "<", ">", "=", "!",           /* 比较/赋值 */
    "&", "|", "^", "~",           /* 位运算 */
    "?", ":",                       /* 三元 */
    "#",                             /* 私有字段 */
    "`",                             /* 模板串 */
    "'", "\"",                     /* 字符串引号 */
    "$",                             /* 模板插值 */
    "\\",                           /* 反斜杠（转义） */
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

/* 查看下一个 code point（不解码位置；EOF -1）。UTF-8 逐字节解码。 */
int Lexer::peekChar() const {
  if (pos_ >= source_len_) return -1;
  UChar32 c = 0;
  int32_t i = static_cast<int32_t>(pos_);
  U8_NEXT(source_, i, static_cast<int32_t>(source_len_), c);
  if (c < 0) return -1; /* 非法 UTF-8 序列 */
  return static_cast<int>(c);
}

/* 推进一个 code point（返回该 code point；EOF -1）。col 按 code point 计。
 * \r\n 组合计单个换行：\r 后紧跟 \n 时一并消费（\n 不再 ++line）。 */
int Lexer::nextChar() {
  if (pos_ >= source_len_) return -1;
  UChar32 c = 0;
  int32_t i = static_cast<int32_t>(pos_);
  U8_NEXT(source_, i, static_cast<int32_t>(source_len_), c);
  if (c < 0) return -1;
  pos_ = static_cast<size_t>(i);
  if (c == '\r' && pos_ < source_len_ && source_[pos_] == '\n') {
    /* \r\n：\r 与 \n 构成单个 LineTerminator，\n 一并消费 */
    ++pos_;
  }
  if (isLineTerminator(static_cast<int>(c))) {
    /* 换行：line++，col 复位（\r\n 已在上面整体消费） */
    ++line_;
    col_ = 1;
  } else {
    ++col_;
  }
  return static_cast<int>(c);
}

/* 跳过空白与注释（行注释 / 块注释 / HTML 风格）。HTML 风格注释是**多字符符号**。 */
void Lexer::skipTrivia() {
  for (;;) {
    int c = peekChar();
    if (c == -1) return;
    if (isUnicodeWhitespace(c)) {
      nextChar();
      continue;
    }
    /* LineTerminator（不含 \r\n 组合的特殊处理——由 nextChar 统一） */
    if (isLineTerminator(c)) {
      nextChar();
      continue;
    }
    if (c == '/' && pos_ + 1 < source_len_) {
      int n = static_cast<unsigned char>(source_[pos_ + 1]);
      if (n == '/') {
        /* 行注释：到 LineTerminator 或 EOF */
        nextChar();
        nextChar();
        for (;;) {
          int ch = peekChar();
          if (ch == -1 || isLineTerminator(ch)) break;
          nextChar();
        }
        continue;
      }
      if (n == '*') {
        /* 块注释：读到闭合符；未闭合发错误 */
        nextChar();
        nextChar();
        bool closed = false;
        for (;;) {
          int ch = peekChar();
          if (ch == -1) break;
          if (ch == '*' && pos_ + 1 < source_len_ &&
              static_cast<unsigned char>(source_[pos_ + 1]) == '/') {
            nextChar();
            nextChar();
            closed = true;
            break;
          }
          nextChar();
        }
        if (!closed) {
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated block comment",
                      {line_, col_, pos_}});
        }
        continue;
      }
    }
    /* HTML 风格注释：<!-- 到行尾（Annex B）；--> 仅行首（由 Parser 判 ASI，这里先跳过行尾 -->） */
    if (c == '<' && pos_ + 3 < source_len_ &&
        std::memcmp(source_ + pos_, "<!--", 4) == 0) {
      nextChar();
      nextChar();
      nextChar();
      nextChar();
      for (;;) {
        int ch = peekChar();
        if (ch == -1 || isLineTerminator(ch)) break;
        nextChar();
      }
      continue;
    }
    /* 单个 / 或 <：不是注释，停止跳过 */
    break;
  }
}

/* 入口级 hashbang（M1.5）：固定位置（源码开头 #!），明确结尾（首个
 * LineTerminator）。跳过整行（含换行符），之后首个 token 从下一行起。
 * 仅 #! 开头才做；不产 token、不做词法分析（Parser 入口调用一次）。 */
void Lexer::skipHashbang() {
  if (source_len_ < 2 || source_[0] != '#' || source_[1] != '!') return;
  while (pos_ < source_len_) {
    int c = nextChar();
    if (isLineTerminator(c)) return; /* 已消费换行，跳到下一行 */
  }
}

void Lexer::scanNumber() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::Number;
  const size_t num_start = pos_;

  /* 进制前缀 0x/0o/0b（ASCII 判定） */
  int c = peekChar();
  if (c == '0' && pos_ + 1 < source_len_) {
    int n = static_cast<unsigned char>(source_[pos_ + 1]);
    if (n == 'x' || n == 'X' || n == 'o' || n == 'O' || n == 'b' || n == 'B') {
      nextChar(); /* 0 */
      nextChar(); /* x/o/b */
      /* 十六进制/八进制/二进制数字 + 可选 _ 分隔符 */
      bool any = false;
      for (;;) {
        int d = peekChar();
        if (isHexDigit(d)) {
          nextChar();
          any = true;
        } else if (d == '_') {
          nextChar();
        } else {
          break;
        }
      }
      /* BigInt 后缀 */
      int p = peekChar();
      if (p == 'n') nextChar();
      t.text = std::string(source_ + num_start, pos_ - num_start);
      t.end = {line_, col_ - 1, pos_ - 1};
      ParsedNumber r = parseNumber(t.text);
      if (!any || !r.ok) {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid number literal", t.start});
      } else {
        t.number = r.value;
        t.is_bigint = r.is_bigint;
      }
      current_ = std::move(t);
      return;
    }
  }

  /* 十进制：整数 + 小数 + 指数 + _ 分隔符 + BigInt */
  bool seen_digit = false;
  bool seen_dot = false;
  for (;;) {
    c = peekChar();
    if (isDecimalDigit(c)) {
      nextChar();
      seen_digit = true;
    } else if (c == '_') {
      nextChar();
    } else if (c == '.' && !seen_dot) {
      /* 数字后的 . 且后随数字 → 小数点；否则（如 1.toString）交给符号表 */
      int next = (pos_ + 1 < source_len_) ? static_cast<unsigned char>(source_[pos_ + 1]) : -1;
      if (next >= '0' && next <= '9') {
        seen_dot = true;
        nextChar();
      } else {
        break;
      }
    } else if ((c == 'e' || c == 'E') && seen_digit) {
      nextChar(); /* e */
      int s = peekChar();
      if (s == '+' || s == '-') nextChar();
      /* 指数至少一位数字 */
      if (!isDecimalDigit(peekChar())) {
        t.kind = TokenKind::Error;
        t.text = std::string(source_ + num_start, pos_ - num_start);
        t.end = {line_, col_ - 1, pos_ - 1};
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid number literal", t.start});
        current_ = std::move(t);
        return;
      }
      while (isDecimalDigit(peekChar())) nextChar();
      break;
    } else {
      break;
    }
  }

  /* BigInt 后缀 n */
  if (peekChar() == 'n') nextChar();

  t.text = std::string(source_ + num_start, pos_ - num_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  if (!seen_digit) {
    t.kind = TokenKind::Error;
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid number literal", t.start});
  } else {
    ParsedNumber r = parseNumber(t.text);
    if (!r.ok) {
      t.kind = TokenKind::Error;
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid number literal", t.start});
    } else {
      t.number = r.value;
      t.is_bigint = r.is_bigint;
    }
  }
  current_ = std::move(t);
}

void Lexer::scanIdentifier() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::Identifier;
  const size_t id_start = pos_;

  /* 首字符：ID_Start / _ / $（或 \u 转义） */
  int c = peekChar();
  if (c == '\\') {
    /* \uXXXX / \u{...} 转义首字符 */
    nextChar(); /* \ */
    c = peekChar();
    if (c != 'u') {
      t.kind = TokenKind::Error;
      t.end = {line_, col_ - 1, pos_ - 1};
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier escape", t.start});
      current_ = std::move(t);
      return;
    }
    nextChar(); /* u */
    if (peekChar() == '{') {
      nextChar();
      int cp = 0;
      int digits = 0;
      for (;;) {
        int d = peekChar();
        if (d == '}') { nextChar(); break; }
        int hv = hexValue(d);
        if (hv < 0 || digits >= 6) {
          t.kind = TokenKind::Error;
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start});
          break;
        }
        cp = cp * 16 + hv;
        ++digits;
        nextChar();
      }
      if (!isIdStart(cp)) {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier start", t.start});
      }
    } else {
      int cp = 0;
      for (int i = 0; i < 4; ++i) {
        int d = peekChar();
        int hv = hexValue(d);
        if (hv < 0) {
          t.kind = TokenKind::Error;
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start});
          break;
        }
        cp = cp * 16 + hv;
        nextChar();
      }
      if (!isIdStart(cp)) {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier start", t.start});
      }
    }
  } else if (!isIdStart(c)) {
    t.kind = TokenKind::Error;
    t.end = {line_, col_ - 1, pos_ - 1};
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier start", t.start});
    current_ = std::move(t);
    return;
  } else {
    nextChar();
  }

  /* 后续字符：ID_Continue / _ / $ / ZWNJ / ZWJ / \u 转义 */
  for (;;) {
    c = peekChar();
    if (isIdContinue(c)) {
      nextChar();
    } else if (c == '\\') {
      nextChar();
      c = peekChar();
      if (c != 'u') {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier escape", t.start});
        break;
      }
      nextChar();
      if (peekChar() == '{') {
        nextChar();
        int cp = 0;
        for (;;) {
          int d = peekChar();
          if (d == '}') { nextChar(); break; }
          int hv = hexValue(d);
          if (hv < 0) {
            t.kind = TokenKind::Error;
            sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start});
            break;
          }
          cp = cp * 16 + hv;
          nextChar();
        }
        if (!isIdContinue(cp)) {
          t.kind = TokenKind::Error;
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier continue", t.start});
        }
      } else {
        int cp = 0;
        for (int i = 0; i < 4; ++i) {
          int d = peekChar();
          int hv = hexValue(d);
          if (hv < 0) {
            t.kind = TokenKind::Error;
            sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start});
            break;
          }
          cp = cp * 16 + hv;
          nextChar();
        }
        if (!isIdContinue(cp)) {
          t.kind = TokenKind::Error;
          sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid identifier continue", t.start});
        }
      }
    } else {
      break;
    }
  }

  t.text = std::string(source_ + id_start, pos_ - id_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  current_ = std::move(t);
}

/* 字符串字面量：quote 为 ' 或 "；处理转义；废弃八进制转义打警告 */
void Lexer::scanString(char quote) {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::String;
  const size_t str_start = pos_;
  nextChar(); /* 开引号 */
  std::string value;

  for (;;) {
    int c = peekChar();
    if (c == -1 || isLineTerminator(c)) {
      /* 未闭合：字符串跨行或到 EOF */
      t.kind = TokenKind::Error;
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated string literal", t.start});
      break;
    }
    if (c == quote) {
      nextChar();
      break;
    }
    if (c == '\\') {
      nextChar(); /* \ */
      int e = peekChar();
      if (e == -1) {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated string literal", t.start});
        break;
      }
      switch (e) {
        case 'n': value += '\n'; nextChar(); break;
        case 't': value += '\t'; nextChar(); break;
        case 'r': value += '\r'; nextChar(); break;
        case 'b': value += '\b'; nextChar(); break;
        case 'f': value += '\f'; nextChar(); break;
        case 'v': value += '\v'; nextChar(); break;
        case '0': value += '\0'; nextChar(); break;
        case '\'': value += '\''; nextChar(); break;
        case '"': value += '"'; nextChar(); break;
        case '\\': value += '\\'; nextChar(); break;
        case 'x': {
          nextChar();
          int h1 = hexValue(peekChar());
          if (h1 < 0) { t.kind = TokenKind::Error; sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid hex escape", t.start}); break; }
          nextChar();
          int h2 = hexValue(peekChar());
          if (h2 < 0) { t.kind = TokenKind::Error; sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid hex escape", t.start}); break; }
          nextChar();
          value += static_cast<char>((h1 << 4) | h2);
          break;
        }
        case 'u': {
          nextChar();
          if (peekChar() == '{') {
            nextChar();
            int cp = 0;
            for (;;) {
              int d = peekChar();
              if (d == '}') { nextChar(); break; }
              int hv = hexValue(d);
              if (hv < 0) { t.kind = TokenKind::Error; sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start}); break; }
              cp = cp * 16 + hv;
              nextChar();
            }
            /* 追加 UTF-8 编码 */
            char buf[4];
            int32_t n = 0;
            U8_APPEND_UNSAFE(buf, n, cp);
            value.append(buf, static_cast<size_t>(n));
          } else {
            int cp = 0;
            for (int i = 0; i < 4; ++i) {
              int d = peekChar();
              int hv = hexValue(d);
              if (hv < 0) { t.kind = TokenKind::Error; sink_.emit({Severity::Error, "SYNTAX_ERROR", "invalid unicode escape", t.start}); break; }
              cp = cp * 16 + hv;
              nextChar();
            }
            char buf[4];
            int32_t n = 0;
            U8_APPEND_UNSAFE(buf, n, cp);
            value.append(buf, static_cast<size_t>(n));
          }
          break;
        }
        default:
          if (e >= '1' && e <= '7') {
            /* 废弃八进制转义（Annex B）：打警告，按八进制解析 */
            sink_.emit({Severity::Warning, "DEP-0003", "legacy octal escape", t.start});
            int oct = 0;
            while (e >= '0' && e <= '7' && oct < 0x1F) {
              oct = oct * 8 + (e - '0');
              nextChar();
              e = peekChar();
            }
            value += static_cast<char>(oct);
            break;
          }
          /* 其余转义：按字面处理 */
          value += static_cast<char>(e);
          nextChar();
          break;
      }
    } else {
      /* 普通字符：追加 UTF-8 */
      int32_t cp = static_cast<int32_t>(c);
      char buf[4];
      int32_t n = 0;
      U8_APPEND_UNSAFE(buf, n, cp);
      value.append(buf, static_cast<size_t>(n));
      nextChar();
    }
  }

  t.text = std::string(source_ + str_start, pos_ - str_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  t.str_value = std::move(value);
  current_ = std::move(t);
}

/* 模板段扫描（共享）：从当前位置起扫描到 ${（has_interpolation=true）
 * 或闭合反引号（false）。不设 kind（由调用方定）；text 起点 tpl_start。
 * 模板串允许 LineTerminator；` 与 ${ 之外逐字符累积。 */
void Lexer::scanTemplateBody(const char *tpl_start, Token &t) {
  std::string value;
  for (;;) {
    int c = peekChar();
    if (c == -1) {
      t.kind = TokenKind::Error;
      sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated template literal", t.start});
      break;
    }
    if (c == '`') {
      nextChar();
      break;
    }
    if (c == '$' && pos_ + 1 < source_len_ &&
        static_cast<unsigned char>(source_[pos_ + 1]) == '{') {
      /* ${ 进入表达式模式（M1.3）；此段是头/续段 */
      nextChar(); /* $ */
      nextChar(); /* { */
      t.has_interpolation = true;
      break;
    }
    if (c == '\\') {
      nextChar();
      int e = peekChar();
      if (e == -1) {
        t.kind = TokenKind::Error;
        sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated template literal", t.start});
        break;
      }
      /* 简化：模板串转义暂按字符串处理（M1.4 完善） */
      switch (e) {
        case 'n': value += '\n'; nextChar(); break;
        case 't': value += '\t'; nextChar(); break;
        case '`': value += '`'; nextChar(); break;
        case '$': value += '$'; nextChar(); break;
        case '\\': value += '\\'; nextChar(); break;
        default: value += static_cast<char>(e); nextChar(); break;
      }
    } else {
      /* 普通字符（含 LineTerminator，模板串允许） */
      int32_t cp = static_cast<int32_t>(c);
      char buf[4];
      int32_t n = 0;
      U8_APPEND_UNSAFE(buf, n, cp);
      value.append(buf, static_cast<size_t>(n));
      nextChar();
    }
  }
  const size_t start_off = static_cast<size_t>(tpl_start - source_);
  t.text = std::string(tpl_start, pos_ - start_off);
  t.end = {line_, col_ - 1, pos_ - 1};
  t.str_value = std::move(value);
}

/* 模板串头：反引号开头 → TemplateStart（对齐 ES TemplateHead）。
 * 拆三 token：TemplateStart（`...${）、TemplatePart/TemplateEnd（续段）。 */
void Lexer::scanTemplate() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::TemplateStart;
  const size_t tpl_start = pos_;
  nextChar(); /* ` */
  scanTemplateBody(source_ + tpl_start, t);
  current_ = std::move(t);
}

/* 模板续段（M1.4 ③-5）：插值表达式结束后调用。当前 token 是表达式后的 `}`
 * （Symbol，pos_ 已在它之后），回退到 `}` 起点将其与后续文本重扫为
 * TemplatePart（}...${，has_interpolation=true）或 TemplateEnd（}...`，false）。
 * text 自 } 起（`}` 本身不属于 quasi 值，先消费掉再扫 body）。 */
void Lexer::rescanTemplatePart() {
  pos_ = current_.start.offset;
  line_ = current_.start.line;
  col_ = current_.start.col;
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::TemplatePart;
  const size_t tpl_start = pos_;
  nextChar(); /* } */
  scanTemplateBody(source_ + tpl_start, t);
  if (!t.has_interpolation) t.kind = TokenKind::TemplateEnd;
  current_ = std::move(t);
  has_peeked_ = false; /* 重扫使任何前瞻缓存失效 */
}

/* 正则字面量：/pattern/flags。仅读值位置调用（M1.3 三义消解）。
 * pattern 内 \\ 转义下一字符；[ ... ] 内 / 不结束；未闭合报错。 */
void Lexer::scanRegex() {
  Token t;
  t.start = {line_, col_, pos_};
  t.kind = TokenKind::RegExp;
  const size_t re_start = pos_;
  nextChar(); /* / */
  bool in_class = false; /* [ ... ] 内的 / 不结束 pattern */
  bool closed = false;

  for (;;) {
    int c = peekChar();
    if (c == -1 || isLineTerminator(c)) break; /* 正则不能跨行 */
    if (c == '\\') {
      nextChar();
      if (peekChar() == -1 || isLineTerminator(peekChar())) break;
      nextChar();
      continue;
    }
    if (c == '[') { in_class = true; nextChar(); continue; }
    if (c == ']') { in_class = false; nextChar(); continue; }
    if (c == '/' && !in_class) { nextChar(); closed = true; break; }
    nextChar();
  }

  if (!closed) {
    t.kind = TokenKind::Error;
    t.end = {line_, col_ - 1, pos_ - 1};
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "unterminated regular expression", t.start});
    t.text = std::string(source_ + re_start, pos_ - re_start);
    current_ = std::move(t);
    return;
  }

  /* flags：标识符继续字符（含 _ $） */
  while (true) {
    int f = peekChar();
    if (f == -1 || !isIdContinue(f)) break;
    nextChar();
  }

  t.text = std::string(source_ + re_start, pos_ - re_start);
  t.end = {line_, col_ - 1, pos_ - 1};
  current_ = std::move(t);
}

/* 将当前 Symbol '/' 重扫为正则（M1.3 re-scan 规则）：
 * Lexer 默认把 / 扫成除法 Symbol；Parser 在读值位置调用此方法。 */
void Lexer::rescanAsRegex() {
  pos_ = current_.start.offset;
  line_ = current_.start.line;
  col_ = current_.start.col;
  scanRegex();
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
    int c = nextChar();
    t.kind = TokenKind::Error;
    t.text = (c != -1) ? std::string(1, static_cast<char>(c)) : "";
    sink_.emit({Severity::Error, "SYNTAX_ERROR", "unexpected character", t.start});
    t.end = {line_, col_ - 1, pos_ - 1};
    current_ = std::move(t);
    return;
  }

  for (size_t i = 0; i < width; ++i) nextChar();
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

  int c = peekChar();
  if (c == -1) {
    t.kind = TokenKind::EndOfFile;
    t.end = t.start;
    current_ = std::move(t);
    return;
  }

  if (isDecimalDigit(c)) {
    scanNumber();
    return;
  }
  /* \uXXXX / \u{...} 转义标识符：首字符是 \，需显式进入 scanIdentifier */
  if (isIdStart(c) || (c == '\\' && pos_ + 1 < source_len_ && source_[pos_ + 1] == 'u')) {
    scanIdentifier();
    return;
  }
  if (c == '\'' || c == '"') {
    scanString(static_cast<char>(c));
    return;
  }
  if (c == '`') {
    scanTemplate();
    return;
  }

  /* 其余一律走符号表（贪婪最长匹配）。/ 一律先扫成除法 Symbol，
   * 由 Parser 在读值位置（parsePrimary）显式请求 rescanAsRegex 重扫为正则。 */
  scanSymbol();
}

/* 拉取下一个 token（推进扫描状态），成为当前 token */
const Token &Lexer::next() {
  if (has_peeked_) {
    /* 有缓存的前瞻结果：提升为当前，并把扫描位置推进到该 token 之后
     * （peek 恢复过 pos，此处必须同步推进，否则重复扫描同一 token） */
    current_ = std::move(peeked_);
    pos_ = peeked_pos_;
    line_ = peeked_line_;
    col_ = peeked_col_;
    has_peeked_ = false;
    return current_;
  }
  scanNext();
  return current_;
}

const Token &Lexer::peek() {
  if (!has_peeked_) {
    /* 保存当前，扫描下一个，再恢复 current 与扫描状态：peek 不改状态。
     * 同时记录扫描完成后的 pos/line/col，供 next() 消费缓存时推进。 */
    Token saved = current_;
    const size_t saved_pos = pos_;
    const size_t saved_line = line_;
    const size_t saved_col = col_;
    scanNext();
    peeked_ = current_;
    peeked_pos_ = pos_;
    peeked_line_ = line_;
    peeked_col_ = col_;
    current_ = std::move(saved);
    pos_ = saved_pos;
    line_ = saved_line;
    col_ = saved_col;
    has_peeked_ = true;
  }
  return peeked_;
}

/* 状态快照（M1.4 ③-4）：完整记录扫描位置与 token 缓存 */
Lexer::State Lexer::save() const {
  State s;
  s.pos = pos_;
  s.line = line_;
  s.col = col_;
  s.peeked_pos = peeked_pos_;
  s.peeked_line = peeked_line_;
  s.peeked_col = peeked_col_;
  s.current = current_;
  s.peeked = peeked_;
  s.has_peeked = has_peeked_;
  return s;
}

void Lexer::restore(const State &s) {
  pos_ = s.pos;
  line_ = s.line;
  col_ = s.col;
  peeked_pos_ = s.peeked_pos;
  peeked_line_ = s.peeked_line;
  peeked_col_ = s.peeked_col;
  current_ = s.current;
  peeked_ = s.peeked;
  has_peeked_ = s.has_peeked;
}

}  // namespace neojs
