/*
 * neo.js CLI 调试入口（M5-B 调试工具）。
 *
 * 产品形态为嵌入引擎（docs/coding-standards.md #1），CLI 仅作调试工具：
 *   --dump-ast <file>      解析文件并输出 AST 文本
 *   --dump-bytecode <file> 解析 + sema + 编译并输出字节码文本
 *   --eval <source>        直接解析给定源码（配合上述 dump 开关）
 *
 * Windows 下用 wmain 取 UTF-16 命令行参数转 UTF-8（见 memory：win32 argv 编码）。
 * 启动时必须先 icu_data_init() 引导 ICU 嵌入式数据，否则 Unicode 解析失效。
 */

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "icu_data.h"
#include "neojs/core/context.h"
#include "neojs/frontend/diagnostics.h"
#include "neojs/frontend/frontend.h"
#include "neojs/frontend/sema.h"
#include "neojs/vm/bytecode.h"
#include "neojs/vm/compiler.h"
#include "neojs/vm/vm.h"

namespace {

using neojs::AST;
using neojs::DiagnosticSink;

/* 读取整个文件为 UTF-8 字符串。失败返回 false。 */
bool readFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

void printDiagnostics(const DiagnosticSink &sink) { sink.print(stderr); }

/* 打印 value 文本（调试用，简化：数字/字符串/布尔/null/undefined 直出）。 */
std::string valueText(neojs::core::Value *v) {
  using neojs::core::BooleanData;
  using neojs::core::NumberData;
  using neojs::core::StringData;
  using neojs::core::ValueKind;
  if (v == nullptr) return "(null)";
  switch (v->kind) {
    case ValueKind::Undefined: return "undefined";
    case ValueKind::Null: return "null";
    case ValueKind::Boolean:
      return static_cast<BooleanData *>(v->data)->value ? "true" : "false";
    case ValueKind::Number:
      return std::to_string(static_cast<NumberData *>(v->data)->number);
    case ValueKind::String:
      return "\"" + static_cast<StringData *>(v->data)->str + "\"";
    case ValueKind::Exception:
      return "Exception: " +
             static_cast<neojs::core::StringData *>(v->data)->str;
    case ValueKind::Function:
      return "function";
    default:
      return "(kind=" + std::to_string(static_cast<int>(v->kind)) + ")";
  }
  return "?";
}

int run(const std::string &source, bool dump_ast, bool dump_bc, bool run_vm) {
  DiagnosticSink sink;
  AST *ast = neojs::parseSource(source.data(), source.size(), sink);
  if (ast == nullptr) {
    printDiagnostics(sink);
    std::fprintf(stderr, "parse failed\n");
    return 1;
  }

  if (dump_ast) {
    std::puts(neojs::dumpAst(*ast).c_str());
  }

  if (dump_bc || run_vm) {
    neojs::sema::Result sema_result;
    if (!neojs::sema::analyze(ast->root, sema_result)) {
      printDiagnostics(sink);
      std::fprintf(stderr, "sema failed\n");
      delete ast;
      return 1;
    }
    neojs::vm::Bytecode bc;
    if (!neojs::vm::compile_program(ast->root, bc)) {
      printDiagnostics(sink);
      std::fprintf(stderr, "compile failed\n");
      delete ast;
      return 1;
    }
    if (dump_bc) {
      std::puts(neojs::vm::dumpBytecode(bc).c_str());
    }
    if (run_vm) {
      neojs::core::Context ctx;
      neojs::vm::Vm vm(ctx);
      neojs::core::Value *result = vm.run(bc);
      std::printf("result: %s\n", valueText(result).c_str());
    }
  }

  delete ast;
  return 0;
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t **wargv) {
#else
int main(int argc, char **argv) {
#endif
  if (icu_data_init() != 0) {
    std::fprintf(stderr, "neojs: failed to initialize ICU data\n");
    return 1;
  }

#if defined(_WIN32)
  /* UTF-16 命令行 → UTF-8（memory：win32 argv 编码经验） */
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr,
                                  nullptr);
    std::string s(static_cast<size_t>(len) > 0 ? static_cast<size_t>(len) - 1
                                               : 0, '\0');
    if (len > 0) {
      WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), len, nullptr,
                          nullptr);
    }
    args.push_back(s);
  }
#else
  std::vector<std::string> args(argv, argv + argc);
#endif

  bool dump_ast = false;
  bool dump_bc = false;
  bool run_vm = false;
  std::string source;
  bool have_source = false;

  /* 参数解析：--dump-ast / --dump-bytecode / --run 可组合；<file> 或 --eval <src> */
  for (int i = 1; i < argc; ++i) {
    const std::string &a = args[i];
    if (a == "--dump-ast") {
      dump_ast = true;
    } else if (a == "--dump-bytecode") {
      dump_bc = true;
    } else if (a == "--run") {
      run_vm = true;
    } else if (a == "--eval") {
      if (i + 1 < argc) {
        source = args[++i];
        have_source = true;
      } else {
        std::fprintf(stderr, "--eval requires an argument\n");
        return 1;
      }
    } else {
      /* 位置参数：视为源码文件 */
      if (!readFile(a, source)) {
        std::fprintf(stderr, "cannot read file: %s\n", a.c_str());
        return 1;
      }
      have_source = true;
    }
  }

  if (!dump_ast && !dump_bc && !run_vm) {
    std::fprintf(stderr,
                 "usage: neojs --dump-ast|--dump-bytecode|--run [--eval <src>|<file>]\n");
    return 1;
  }
  if (!have_source) {
    std::fprintf(stderr, "no source (use --eval <src> or a file argument)\n");
    return 1;
  }

  return run(source, dump_ast, dump_bc, run_vm);
}
