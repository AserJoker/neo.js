# neo.js

neo.js是一个开源，跨平台，嵌入式的JavaScript运行时。

# License

neo.js 采用MIT许可协议

# Quick Start

1. 执行一个来自文件的模块
```c
#include "neojs/engine/context.h"
int main(int argc, char *argv[]) {
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_js_runtime_t rt = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(rt);
  neo_js_context_run(ctx, "./index.js");
  while (neo_js_context_has_task(ctx)) {
    neo_js_context_next_task(ctx);
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}
```
2. 执行JS代码

```c
#include "neojs/engine/exception.h"
#include "neojs/engine/number.h"
#include "neojs/engine/context.h"
int main(int argc, char *argv[]) {
  neo_allocator_t allocator = neo_create_default_allocator();
  neo_js_runtime_t rt = neo_create_js_runtime(allocator);
  neo_js_context_t ctx = neo_create_js_context(rt);
  neo_js_variable_t res =
      neo_js_context_eval(ctx, "1 + 2", "<eval>", NEO_JS_EVAL_INLINE);
  if (res->value->type == NEO_JS_TYPE_EXCEPTION) {
    neo_js_variable_t error = neo_js_context_create_variable(
        ctx, ((neo_js_exception_t)res->value)->error);
    neo_js_error_callback cb = neo_js_context_get_error_callback(ctx);
    cb(ctx, error);
  } else {
    double val = ((neo_js_number_t)res->value)->value;
    printf("1 + 2 = %lf\n", val);
  }
  while (neo_js_context_has_task(ctx)) {
    neo_js_context_next_task(ctx);
  }
  neo_allocator_free(allocator, ctx);
  neo_allocator_free(allocator, rt);
  neo_delete_allocator(allocator);
  return 0;
}
```
# 源码目录与架构划分
1. core
   core目录下提供了neo.js所需的非JavaScript语言相关内容，例如UTF16字符串，JSON，内存分配器，数据容器等
2. compiler
   compiler目录下提供了一个JavaScript源码编译器，用于将文本的JavaScript编译为AST再由代码生成器转换为字节码
   字节码支持的指令见compiler/asm.h
3. engine
   engine目录下实现JavaScript语言特性并提供了Context，Runtime之类的运行时结构用于访问管理JavaScript运行时信息
4. runtime
   runtime目录下实现了一个可以解析运行编译器生成的字节码的虚拟机，桥接源码与engine。同时runtime目录下还包含了JavaScript语言标准库。
# 源码，接口设计
## 概念定义
   1. Runtime
   Runtime是neo.js运行时对象，包含全局只读信息，例如内存分配器。源码编译后的缓存等。用于全局只读数据缓存
   2. Context
   Context是neo.js上下文，包含运行时信息，例如当前作用域，完整作用域树，任务队列，调用栈等。其中关于作用域，同一时间只有一个被激活的作用域链，即当前作用域以及其父作用域到根作用域的所有作用域
   3. Scope
   Scope是一个运行时作用域，包含一系列具名或者匿名的变量。作用域以树的结构组织，子节点与父节点存在单向生命周期依赖
   4. Variable
   Variable是一个内存变量对象，包含了一个变量生命周期管理钩子，Variable之间通过图结构进行关联，不存在孤立的非根节点变量。Variable与Scope之间为依赖股关系，Scope中持有一个根Variable节点，当前作用域中所有的Variable依赖自Scope中的根Variable
   5. Value
   Value指一个具体的值，可以是简单的string，boolean，number等，也可以是object，function等复杂对象，value之间除闭包外无关系
