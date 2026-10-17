/*
 * neo.js CLI 入口（调试工具，非产品形态——产品形态为嵌入引擎，见规范 #1）。
 * M0：仅提供 --version。
 */

#include <cstdio>
#include <cstring>

namespace {

constexpr const char *kVersion = "0.1.0";

void printVersion() {
  std::printf("neo.js v%s (M0 skeleton)\n", kVersion);
}

void printUsage() {
  std::printf("usage: neojs [--version]\n");
  std::printf("  --version   print version and exit\n");
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    printVersion();
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
