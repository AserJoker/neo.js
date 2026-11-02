// neojs.h - neo.js 唯一对外 C API（M0 占位）
//
// 本头文件是 neo.js 嵌入库对外的唯一入口。M0 阶段仅提供占位声明，
// M1 起充实 Value/Context/Engine 等 API（详见 ADR-009）。
//
// 命名约定（ADR-009）：
//   - C API 类型/函数以 NJS_ 前缀
//   - 不透明指针（opaque pointer）封装内部对象
//
// 关联 ADR: 009（源码结构）、005（native 等价）、006（异常即值）

#ifndef NEOJS_H
#define NEOJS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 不透明类型（M1 起充实）
typedef struct NJS_Context NJS_Context;
typedef struct NJS_Value   NJS_Value;
typedef struct NJS_Engine  NJS_Engine;

// 错误码（ADR-006：异常即值；C API 边界用错误码）
typedef enum {
    NJS_OK = 0,
    NJS_ERROR = 1,
    NJS_PANIC = 2
} NJS_Result;

// M0 占位：返回版本字符串
const char* NJS_Version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NEOJS_H
