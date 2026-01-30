#include "neojs/engine/context.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#endif
int main(int argc, char *argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#else
  setlocale(LC_ALL, "");
#endif
  neo_js_context_t ctx = neo_create_js_context(NULL);
  neo_js_context_run(ctx, "../index.mjs");
  while (neo_js_context_has_task(ctx)) {
    neo_js_context_next_task(ctx);
  }
  neo_delete_js_context(ctx);
  return 0;
}