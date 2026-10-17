#ifndef NEOJS_NEQJS_H
#define NEOJS_NEQJS_H

/*
 * neo.js —— 嵌入式 JavaScript 引擎 · C 对外接口
 *
 * 设计约定（见 docs/coding-standards.md）：
 *   #1 对外接口必须使用 C（稳定 C ABI），内部实现使用 C++。
 *   #2 类型命名无下划线（JSValue/JSContext），方法命名携带下划线（JS_CreateNumber）。
 *   #4 JSValue 是一等公民，大部分 API 以 JSValue 作为返回值；
 *      编译/执行错误通过 kind = JS_EXCEPTION 的 JSValue 传递。
 *   #5 JSValue / JSContext 永远是不透明指针，只能通过访问方法操作。
 *   #9 运行时一切信息都从 ctx 出发，API 首参为 ctx。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* 类型：不透明指针                                                           */
/* -------------------------------------------------------------------------- */

typedef struct JSValue JSValue;        /* JS 值（不透明） */
typedef struct JSContext JSContext;    /* JS 运行时上下文（不透明） */
typedef struct JSAllocator JSAllocator; /* 内存分配器（不透明，#13） */

/* -------------------------------------------------------------------------- */
/* JS 值 kind                                                                    */
/* -------------------------------------------------------------------------- */

typedef enum JSKind {
  JS_UNDEFINED = 0,
  JS_NULL,
  JS_BOOLEAN,
  JS_NUMBER,
  JS_STRING,
  JS_OBJECT,
  JS_EXCEPTION,        /* #4：错误值 */
  JS_INTERRUPT_YIELD,  /* #7：生成器 yield 断点 */
  JS_INTERRUPT_AWAIT,  /* #7：async await 断点 */
} JSKind;

/* -------------------------------------------------------------------------- */
/* 分配器（#13）：不透明 JSAllocator                                          */
/* -------------------------------------------------------------------------- */

/* 创建分配器。宿主可注入自定义 alloc/free 回调（#5）；传 NULL 则用引擎默认。
 * oomAbort 由分配器自己处理：allocate 失败时分配器内部直接终止（#14）。 */
JSAllocator *JS_NewAllocator(void *(*alloc)(size_t size, void *userdata),
                             void (*free_fn)(void *ptr, void *userdata),
                             void *userdata);

/* 释放分配器。注意：必须先于所有使用它的 ctx 释放。 */
void JS_FreeAllocator(JSAllocator *allocator);

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

/* 创建一个新运行时上下文，**必须**提供宿主创建的 JSAllocator（#13）。
 * allocator 不支持 NULL（宿主先 JS_NewAllocator 再创建 ctx）。 */
JSContext *JS_NewContext(JSAllocator *allocator);

/* 释放运行时上下文及其管理的所有资源。
 * 注意：ctx 不拥有 allocator，调用方需自行 JS_FreeAllocator（且应在释放 ctx 之后）。 */
void JS_FreeContext(JSContext *ctx);

/* 压栈/弹栈作用域（#9 作用域由 ctx 管理）。
 * JS_PushScope：压入一个子作用域并成为当前作用域；后续创建的值归属它。
 * JS_PopScope：销毁当前作用域（回收其归属值），回到父作用域。根作用域不可弹栈。
 * 注意：作用域构成 parent/children 树；协程/异步挂起时**不销毁**当前作用域，
 * 而是经 parent/children 体系保留子树，resume 时继续使用。 */
void JS_PushScope(JSContext *ctx);
void JS_PopScope(JSContext *ctx);

/* 按名加载值：沿当前作用域链（本作用域向上到根）查找 name 绑定（#9）。
 * 未找到时返回 kind = JS_EXCEPTION 的错误值（#4）；永不返回 NULL（#14）。 */
JSValue *JS_LoadValue(JSContext *ctx, const char *name);

/* 定义绑定：将 value 以 name 定义到**当前作用域**（对应 let/const/var 声明）。
 * 返回 value；永不返回 NULL（#14）。 */
JSValue *JS_DefineValue(JSContext *ctx, const char *name, JSValue *value);

/* 存储绑定：沿当前作用域链更新**已存在**的 name 绑定为 value（对应赋值）。
 * 链上无此名时返回 kind = JS_EXCEPTION 的错误值（ReferenceError，#4）。
 * 返回更新后的 value 或错误值；永不返回 NULL（#14）。 */
JSValue *JS_StoreValue(JSContext *ctx, const char *name, JSValue *value);

/* -------------------------------------------------------------------------- */
/* 值构造与访问（#2 命名风格：JS_ 前缀 + 动作）                                */
/* -------------------------------------------------------------------------- */

JSValue *JS_CreateUndefined(JSContext *ctx);
JSValue *JS_CreateNull(JSContext *ctx);
JSValue *JS_CreateBoolean(JSContext *ctx, int value);
JSValue *JS_CreateNumber(JSContext *ctx, double value);
JSValue *JS_CreateInteger(JSContext *ctx, int64_t value);
JSValue *JS_CreateString(JSContext *ctx, const char *str, size_t len);

/* 释放一个值。由调用方负责（与 ctx 的 alloc/free 对应）。 */
void JS_FreeValue(JSContext *ctx, JSValue *value);

/* 返回值的 kind（JS_UNDEFINED / JS_EXCEPTION / ...）。 */
JSKind JS_GetKind(JSContext *ctx, const JSValue *value);

/* 值访问（仅当 kind 匹配时有效；否则返回 0 / NULL）。 */
int JS_ToBoolean(JSContext *ctx, const JSValue *value);
double JS_ToNumber(JSContext *ctx, const JSValue *value);
int64_t JS_ToInteger(JSContext *ctx, const JSValue *value);
const char *JS_ToString(JSContext *ctx, const JSValue *value); /* 返回内部 UTF-8 缓冲，生命周期归 ctx */

/* 构造一个错误值（kind = JS_EXCEPTION）。message 会被复制。 */
JSValue *JS_CreateError(JSContext *ctx, const char *message);

/* -------------------------------------------------------------------------- */
/* 执行（M0 占位：完整求值引擎在后续里程碑实现）                               */
/* -------------------------------------------------------------------------- */

/* 在 ctx 中求值一段源码。当前 M0 返回 kind=JS_EXCEPTION 的错误值表示
 * “求值器尚未实现”，以验证错误通道。 */
JSValue *JS_Eval(JSContext *ctx, const char *source, size_t source_len);

/* -------------------------------------------------------------------------- */
/* 宿主注入 C 函数（#6）—— M0 预留签名，实际注册在后续里程碑               */
/* -------------------------------------------------------------------------- */

/* 宿主 C 函数签名：#9 首参为 ctx；#4 返回 JSValue *。 */
typedef JSValue *(*JSCFunction)(JSContext *ctx, JSValue *self, size_t argc,
                                JSValue *argv);

#ifdef __cplusplus
}
#endif

#endif /* NEOJS_NEQJS_H */
