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
  JS_SYMBOL,
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
 * kind 指定绑定类型（0=Var, 1=Let, 2=Const, 3=Function, 4=Param, 5=Catch），
 * M0 只记录不校验（为 M2 的 const/TDZ 打地基）。
 * value 为 NULL 时定义未初始化绑定（TDZ 起点），此时返回 NULL（#14 语义例外）。
 * 返回 value；否则永不返回 NULL（#14）。 */
JSValue *JS_DefineValue(JSContext *ctx, const char *name, JSValue *value, int kind);

/* 存储绑定：沿当前作用域链更新**已存在**的 name 绑定为 value（对应赋值）。
 * 链上无此名时返回 kind = JS_EXCEPTION 的错误值（ReferenceError，#4）。
 * 返回更新后的 value 或错误值；永不返回 NULL（#14）。 */
JSValue *JS_StoreValue(JSContext *ctx, const char *name, JSValue *value);

/* -------------------------------------------------------------------------- */
/* 值构造与访问（#2 命名风格：JS_ 前缀 + 动作）                                */
/* -------------------------------------------------------------------------- */

/* 值构造。所有 CreateX 的 name 参数二选一（docs/gc.md G2 + 无孤立值原则）：
 *   name == NULL  创建作用域**临时值**：仅宿主句柄持有（宿主负责 JS_FreeValue）；
 *   name != NULL  创建作用域**具名值**：自动定义绑定到当前作用域（kind=Let，
 *                块级绑定；非 var 的函数级提升）。返回值仍为句柄
 *                （宿主可 FreeValue，值由绑定持有仍存活）。
 * 不存在创建后既无句柄也无绑定的孤立值。 */
JSValue *JS_CreateUndefined(JSContext *ctx, const char *name);
JSValue *JS_CreateNull(JSContext *ctx, const char *name);
JSValue *JS_CreateBoolean(JSContext *ctx, int value, const char *name);
JSValue *JS_CreateNumber(JSContext *ctx, double value, const char *name);
JSValue *JS_CreateInteger(JSContext *ctx, int64_t value, const char *name);
JSValue *JS_CreateString(JSContext *ctx, const char *str, size_t len, const char *name);
JSValue *JS_CreateSymbol(JSContext *ctx, const char *description, const char *name);

/* 释放一个值。由调用方负责（与 ctx 的 alloc/free 对应）。 */
void JS_FreeValue(JSContext *ctx, JSValue *value);

/* 返回值的 kind（JS_UNDEFINED / JS_EXCEPTION / ...）。 */
JSKind JS_GetKind(JSContext *ctx, const JSValue *value);

/* 值访问（仅当 kind 匹配时有效；否则返回 0 / NULL）。 */
int JS_ToBoolean(JSContext *ctx, const JSValue *value);
double JS_ToNumber(JSContext *ctx, const JSValue *value);
int64_t JS_ToInteger(JSContext *ctx, const JSValue *value);
const char *JS_ToString(JSContext *ctx, const JSValue *value); /* 返回内部 UTF-8 缓冲，生命周期归 ctx */

/* 构造一个错误值（kind = JS_EXCEPTION）。message 会被复制。
 * name 语义同其它 CreateX：NULL = 临时值，非空 = 绑定到当前作用域。 */
JSValue *JS_CreateError(JSContext *ctx, const char *message, const char *name);

/* -------------------------------------------------------------------------- */
/* 全局对象与 well-known symbol（E4）                                          */
/* -------------------------------------------------------------------------- */

/* 全局对象（globalThis 雏形）。首次调用时创建并注册 E4 内置（Symbol 构造器
 * 等）。返回句柄，宿主负责 JS_FreeValue。 */
JSValue *JS_GetGlobalObject(JSContext *ctx);

/* 取 well-known symbol（架构 §5.2）：index 0=iterator, 1=toPrimitive,
 * 2=toStringTag, 3=hasInstance。返回句柄（同一 symbol 每次调用同一句柄？
 * 否——每次分配新句柄指向同一内部值；宿主负责 FreeValue）。 */
JSValue *JS_GetWellKnownSymbol(JSContext *ctx, int index);

/* -------------------------------------------------------------------------- */
/* 对象模型（E1 对象模型地基；架构 §5.2）                                     */
/* -------------------------------------------------------------------------- */

/* 创建普通对象（kind = JS_OBJECT）。name 语义同其它 CreateX。
 * prototype 为 NULL 时使用默认 Object.prototype（M4 内置注册后生效；
 * 当前阶段默认原型为 NULL，原型链仅经 JS_SetPrototype 显式建立）。 */
JSValue *JS_CreateObject(JSContext *ctx, JSValue *prototype, const char *name);

/* 创建数组对象（kind = JS_OBJECT，数组 elements 分离存储，架构 §5.2）。 */
JSValue *JS_CreateArray(JSContext *ctx, const char *name);

/* 属性访问（架构 §5.2 ordinary [[Get]]/[[Set]]，沿原型链）。
 * key 为属性名（string）。value 为宿主句柄。
 * JS_GetProperty：未找到返回 JS_UNDEFINED 值（#14 非 NULL）；
 * JS_SetProperty：返回被赋的 value；key 不存在时新增属性。
 * JS_HasProperty：返回 1/0（沿原型链）。
 * JS_DeleteProperty：返回 1（删除成功）/0（属性不存在）。 */
JSValue *JS_GetProperty(JSContext *ctx, JSValue *obj, const char *key);
JSValue *JS_SetProperty(JSContext *ctx, JSValue *obj, const char *key, JSValue *value);
int JS_HasProperty(JSContext *ctx, JSValue *obj, const char *key);
int JS_DeleteProperty(JSContext *ctx, JSValue *obj, const char *key);

/* 原型链访问（架构 §5.2）。JS_GetPrototype 返回原型对象值；
 * JS_SetPrototype 设置 [[Prototype]]（返回 1 成功 / 0 失败）。 */
JSValue *JS_GetPrototype(JSContext *ctx, JSValue *obj);
int JS_SetPrototype(JSContext *ctx, JSValue *obj, JSValue *proto);

/* 数组元素访问（架构 §5.2 elements 分离存储 + length 推导）。
 * JS_GetElement：越界/空洞返回 JS_UNDEFINED 值。
 * JS_SetElement：越界自动扩容。
 * JS_GetLength / JS_SetLength：数组长度（元素存储大小）。 */
JSValue *JS_GetElement(JSContext *ctx, JSValue *arr, size_t index);
JSValue *JS_SetElement(JSContext *ctx, JSValue *arr, size_t index, JSValue *value);
size_t JS_GetLength(JSContext *ctx, JSValue *arr);
int JS_SetLength(JSContext *ctx, JSValue *arr, size_t length);

/* -------------------------------------------------------------------------- */
/* GC 引擎（docs/gc.md）                                                      */
/* -------------------------------------------------------------------------- */

/* 手动触发一次完整 GC（mark-sweep，docs/gc.md §5.1）。
 * 从根集（句柄表 + 作用域绑定链 + 未来 VM 栈帧等）标记可达对象，回收其余。
 * 宿主持有的句柄在 GC 后仍有效（G2/G4）。 */
void JS_GC(JSContext *ctx);

/* 设置自动 GC 分配阈值：累计分配达到 threshold 个值后自动触发一次 GC。
 * 传 0 关闭自动触发（仅手动 JS_GC）。默认 1000。 */
void JS_SetGCThreshold(JSContext *ctx, size_t threshold);

/* -------------------------------------------------------------------------- */
/* 运算符语义（E4-3，ECMA-262 §7.2/§7.3/§13）                                 */
/* -------------------------------------------------------------------------- */

/* 严格相等比较（§7.2.14）：x === y。永不抛异常，返回 1（相等）/0（不等）。 */
int JS_StrictEqual(JSContext *ctx, JSValue *x, JSValue *y);

/* 抽象相等比较（§7.2.15）：x == y。返回 Boolean 值；异常返回 Exception 值。 */
JSValue *JS_Equal(JSContext *ctx, JSValue *x, JSValue *y);

/* 抽象关系比较（§7.2.13 IsLessThan）：x < y。返回 Boolean 值；异常返回 Exception。 */
JSValue *JS_RelationalCompare(JSContext *ctx, JSValue *x, JSValue *y);

/* 加法运算符（§13.15.2）：x + y。返回 Number 或 String 值；异常返回 Exception。 */
JSValue *JS_Add(JSContext *ctx, JSValue *x, JSValue *y);

/* typeof 运算符（§13.5.3）：返回类型名 String 值（"undefined"/"object"/...）。 */
JSValue *JS_Typeof(JSContext *ctx, JSValue *x);

/* instanceof 运算符（§7.3.22）：x instanceof y。返回 Boolean 值；异常返回 Exception。 */
JSValue *JS_Instanceof(JSContext *ctx, JSValue *x, JSValue *y);

/* in 运算符（§7.3.20 HasProperty）：x in y。返回 Boolean 值；异常返回 Exception。 */
JSValue *JS_In(JSContext *ctx, JSValue *x, JSValue *y);

/* -------------------------------------------------------------------------- */
/* 执行（M1.1：前端流水线骨架；M2-VM 字节码执行入口）                        */
/* -------------------------------------------------------------------------- */

/* 在 ctx 中求值一段源码。当前 M1.1 阶段：源码经前端流水线（Lexer+Parser）
 * 解析，返回 AST dump 文本（kind=JS_STRING）；语法错误返回 kind=JS_EXCEPTION
 * 的错误值（#4）。完整求值（字节码 VM）在 M2+ 实现。 */
JSValue *JS_Eval(JSContext *ctx, const char *source, size_t source_len);

/* 指令操作码（与内部 neojs::Opcode 数值一致，M2-VM）。供 JS_RunBytecode
 * 构造字节码；arg 语义见各指令：PushConst/LoadGlobal 为常量池索引，
 * LoadVar/StoreVar 为局部槽号，Jump/JumpIfFalse 为绝对指令位置，
 * Call 为参数个数。 */
typedef enum JSBCOp {
  JS_BC_PUSH_CONST = 0,
  JS_BC_PUSH_UNDEFINED,
  JS_BC_PUSH_NULL,
  JS_BC_PUSH_TRUE,
  JS_BC_PUSH_FALSE,
  JS_BC_PUSH_INT,
  JS_BC_LOAD_VAR,
  JS_BC_STORE_VAR,
  JS_BC_LOAD_GLOBAL,
  JS_BC_POP,
  JS_BC_DUP,         /* 复制栈顶（M4.1 成员调用 this） */
  JS_BC_ADD,
  JS_BC_SUB,
  JS_BC_MUL,
  JS_BC_DIV,
  JS_BC_MOD,
  JS_BC_POW,
  JS_BC_NEG,
  JS_BC_NOT,
  JS_BC_TYPEOF,
  JS_BC_EQ,
  JS_BC_STRICT_EQ,
  JS_BC_LT,
  JS_BC_LE,
  JS_BC_GT,
  JS_BC_GE,
  JS_BC_NE,
  JS_BC_STRICT_NE,
  JS_BC_BIT_AND,
  JS_BC_BIT_OR,
  JS_BC_BIT_XOR,
  JS_BC_LSHIFT,
  JS_BC_RSHIFT,
  JS_BC_URSHIFT,
  JS_BC_JUMP,
  JS_BC_JUMP_IF_FALSE,
  JS_BC_CALL,
  JS_BC_CALL_METHOD,  /* 成员调用（M4.1）：obj, func, args... this=obj */
  JS_BC_RET,
  JS_BC_MAKE_FUNCTION,  /* M2.5 闭包：MakeFunction {index} */
  JS_BC_LOAD_UPVALUE,    /* 捕获的外层变量读取（M2.5） */
  JS_BC_STORE_UPVALUE,   /* 捕获的外层变量写入（M2.5） */
  JS_BC_NEW_OBJECT,      /* 新建空对象（M3） */
  JS_BC_NEW_ARRAY,       /* 新建数组（M3） */
  JS_BC_LOAD_PROP,       /* obj.key -> 栈顶（M3） */
  JS_BC_STORE_PROP,      /* obj, val -> val（M3） */
  JS_BC_LOAD_ELEM,       /* obj, key -> 栈顶（M3） */
  JS_BC_STORE_ELEM,      /* obj, key, val -> val（M3） */
  JS_BC_PRINT,
  JS_BC_NOP,
} JSBCOp;

/* 执行一段字节码（M2-VM，架构 §5.4）：
 *   ops        指令数组（JSBCOp）；ops_len 为指令条数
 *   args       每条指令的立即数（uint16_t，与 JSBCOp 一一对应）
 *   constants  常量池（字符串数组，PushConst/LoadGlobal 经索引引用）
 *   const_len  常量池条数
 *   num_locals 局部变量槽数
 *   argv/argc  入口参数（宿主句柄数组；可 NULL/0）
 * 返回执行结果值句柄；错误返回 kind=JS_EXCEPTION（#4）。
 * 注意：ops 指向的数组在调用期间有效即可（内部复制），调用后可释放。 */
JSValue *JS_RunBytecode(JSContext *ctx, const JSBCOp *ops, size_t ops_len,
                        const uint16_t *args, const char *const *constants,
                        size_t const_len, uint16_t num_locals, size_t argc,
                        JSValue *const *argv);

/* -------------------------------------------------------------------------- */
/* 宿主注入 C 函数（#6）—— M0 预留签名，实际注册在后续里程碑               */
/* -------------------------------------------------------------------------- */

/* 宿主 C 函数签名：#9 首参为 ctx；#4 返回 JSValue *。 */
typedef JSValue *(*JSCFunction)(JSContext *ctx, JSValue *self, size_t argc,
                                JSValue *argv);

/* 创建宿主 C 函数值（kind = JS_OBJECT，可调用对象）。
 * func 为 JSCFunction 回调；name 为函数名（可空）。
 * name 语义同其它 CreateX：NULL = 临时值，非空 = 绑定到当前作用域。 */
JSValue *JS_NewCFunction(JSContext *ctx, JSCFunction func, const char *name);

/* 调用函数（§7.3.12 Call）：func 为函数值（宿主句柄），this_val 为 this 绑定
 * （可为 NULL，非严格模式绑到全局对象），argv 为参数数组（宿主句柄指针），
 * argc 为参数个数。
 * 返回结果值句柄；func 不可调用时返回 kind = JS_EXCEPTION 的错误值（#4）。 */
JSValue *JS_Call(JSContext *ctx, JSValue *func, JSValue *this_val, size_t argc,
                 JSValue *const *argv);

#ifdef __cplusplus
}
#endif

#endif /* NEOJS_NEQJS_H */
