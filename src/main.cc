/*
 * neo.js CLI 入口（调试工具，非产品形态——产品形态为嵌入引擎，见规范 #1）。
 * M0：--version；M1.1：--dump-tokens / --dump-ast（前端流水线可观测性，架构 §1.3）。
 */

#include <cstdio>
#include <cstring>

#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/neojs.h"
#include "neojs/runtime/allocator.h"
#include "neojs/runtime/context.h"

namespace {

constexpr const char *kVersion = "0.1.0";

void printVersion() {
  std::printf("neo.js v%s (M1.1 frontend skeleton)\n", kVersion);
}

void printUsage() {
  std::printf("usage: neojs [--version]\n");
  std::printf("       neojs --dump-tokens '<source>'\n");
  std::printf("       neojs --dump-ast '<source>'\n");
  std::printf("  --version      print version and exit\n");
  std::printf("  --dump-tokens  tokenize source and print the token stream\n");
  std::printf("  --dump-ast     parse source and print the AST\n");
}

/* 打印诊断到 stderr（架构 §7：默认输出 stderr）。返回是否无错误。 */
bool printDiagnostics(neojs::DiagnosticSink &sink) {
  if (sink.diagnostics().empty()) return true;
  sink.print(stderr);
  return !sink.hasErrors();
}

/* 创建默认分配器 + ctx；调用方负责 JS_FreeContext 后 JS_FreeAllocator。 */
void makeEngine(JSAllocator **out_alloc, JSContext **out_ctx) {
  JSAllocator *a = JS_NewAllocator(nullptr, nullptr, nullptr);
  JSContext *ctx = JS_NewContext(a);
  *out_alloc = a;
  *out_ctx = ctx;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    printVersion();
    return 0;
  }

  if (argc == 3 && std::strcmp(argv[1], "--dump-tokens") == 0) {
    neojs::DiagnosticSink sink;
    std::string out = neojs::dumpTokens(argv[2], std::strlen(argv[2]), sink);
    if (!printDiagnostics(sink)) return 1;
    std::printf("%s", out.c_str());
    return 0;
  }

  if (argc == 3 && std::strcmp(argv[1], "--dump-ast") == 0) {
    JSAllocator *a = nullptr;
    JSContext *ctx = nullptr;
    makeEngine(&a, &ctx);
    /* CLI 是引擎内部工具：经 ctx 内部实现取 JSAllocator（#13）。 */
    neojs::JSContext *c = reinterpret_cast<neojs::JSContext *>(ctx);
    neojs::DiagnosticSink sink;
    neojs::AST *ast = neojs::parseSource(&c->allocator(), argv[2],
                                         std::strlen(argv[2]), sink);
    if (ast == nullptr) {
      printDiagnostics(sink);
      JS_FreeContext(ctx);
      JS_FreeAllocator(a);
      return 1;
    }
    std::string dump = neojs::dumpAst(*ast);
    c->allocator().destroy(ast);
    JS_FreeContext(ctx);
    JS_FreeAllocator(a);
    if (!printDiagnostics(sink)) return 1;
    std::printf("%s", dump.c_str());
    return 0;
  }

  if (argc == 1) {
    /* 无参数：进入 REPL（M1 实现）；当前打印提示 */
    std::printf("neo.js REPL not implemented yet (M1)\n");
    return 0;
  }

  printUsage();
  return 1;
}
