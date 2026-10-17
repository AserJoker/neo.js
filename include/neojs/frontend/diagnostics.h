#ifndef NEOJS_FRONTEND_DIAGNOSTICS_H
#define NEOJS_FRONTEND_DIAGNOSTICS_H

/*
 * 前端诊断通道（架构 §7：DiagnosticSink）。
 *
 * Lexer / Parser 识别到错误或废弃语法时经 sink 发出诊断（不中止解析），
 * 调用方（CLI / JS_Eval / 后续 Compiler）决定如何呈现。
 *
 * 设计要点（docs/milestones.md M1.1）：
 *   - Diagnostic{severity, code, message, source location}。
 *   - Severity：Error（SyntaxError 等）、Warning（deprecation）、Info（trace）。
 *   - Parser 识别废弃语法时不中止，只记录 Warning（架构 §7.2）。
 *   - 默认输出格式：`<line>:<col>: <severity>: [<code>] <message>`。
 */

#include <cstdio>
#include <string>
#include <vector>

#include "neojs/frontend/ast.h"

namespace neojs {

/* 严重级别（架构 §7.1） */
enum class Severity : uint8_t {
  Error,   /* SyntaxError 等 */
  Warning, /* deprecation */
  Info,    /* trace */
};

/* 单条诊断（架构 §7.1：severity/code/message/location） */
struct Diagnostic {
  Severity severity = Severity::Error;
  std::string code;    /* 例如 "SYNTAX_ERROR"、"DEP-0001"（废弃特性） */
  std::string message;
  SourceLocation loc;
};

/* 诊断收集器：Lexer/Parser 共用；不负责中止解析，只记录 */
class DiagnosticSink {
 public:
  void emit(const Diagnostic &d) { diagnostics_.push_back(d); }

  bool hasErrors() const {
    for (const Diagnostic &d : diagnostics_) {
      if (d.severity == Severity::Error) return true;
    }
    return false;
  }

  const std::vector<Diagnostic> &diagnostics() const { return diagnostics_; }

  /* 打印全部诊断到 out（默认 stderr）。格式：
   *   <line>:<col>: <error|warning|info>: [<code>] <message> */
  void print(FILE *out) const;

 private:
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace neojs

#endif  // NEOJS_FRONTEND_DIAGNOSTICS_H
