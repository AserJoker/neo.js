#ifndef NEOJS_FRONTEND_FRONTEND_H
#define NEOJS_FRONTEND_FRONTEND_H

/*
 * 前端流水线统一入口（M1.1）：源码 → Lexer+Parser → AST（或 dump 文本）。
 *
 * 供 CLI（--dump-tokens/--dump-ast）与 JS_Eval 使用。诊断经 DiagnosticSink 收集。
 *
 * 内存：返回的 AST* 由调用方负责释放（delete）；
 * dump 文本为值返回（std::string），不涉及分配器归属。
 */

#include <string>

#include "neojs/frontend/ast.h"
#include "neojs/frontend/diagnostics.h"

namespace neojs {

/* 解析源码：返回 AST*（成功）或 nullptr（语法错误，诊断在 sink）。
 * module=true 时按 ES Module 解析（Module 根节点 + 自动严格模式 + import/export）。
 * 调用方负责释放返回的 AST。 */
AST *parseSource(const char *source, size_t source_len,
                 DiagnosticSink &sink, bool module = false);

/* 全量 token 化并输出文本（--dump-tokens）。不依赖 Parser；
 * 遍历整个 token 流（协同模式下 Lexer 由 Parser 驱动，此处独立驱动）。 */
std::string dumpTokens(const char *source, size_t source_len, DiagnosticSink &sink);

/* 输出 AST 文本（--dump-ast / JS_Eval 结果）。 */
std::string dumpAst(const AST &ast);

}  // namespace neojs

#endif  // NEOJS_FRONTEND_FRONTEND_H
