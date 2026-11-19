// neojs_intl_icu.h - ICU 版 Intl 后端（独立静态库）
//
// 本头文件属于可选的 neojs_intl_icu 静态库。core 本身不链接 ICU；应用按需
// 链接本库并把 NJS_SelectIntlBackend() 的结果传给 createContext，即可获得
// ICU 的 locale 数据与格式化能力。
//
// 三级回退（mini → ICU → custom）：
//   1. custom：应用自己实现的 NJS_IntlBackend，优先级最高；
//   2. ICU：链接本库时由 NJS_IcuIntlBackend() 提供；
//   3. mini：未链接本库、或 ICU 初始化失败时传 nullptr，core 使用内置最小后端。
//
// 典型用法：
//   const NJS_IntlBackend* intl = NJS_SelectIntlBackend(myCustomBackend);
//   auto ctx = neo::core::createContext(alloc, console, clock, intl);
//
// 使用 vendored ICU（third_party/icu）时，本库会在首次调用时自动执行
// icu_data_init()，无需应用额外处理。

#ifndef NEOJS_INTL_ICU_H
#define NEOJS_INTL_ICU_H

#include "neojs.h"

#ifdef __cplusplus
extern "C" {
#endif

// 返回 ICU 后端单例；ICU 初始化失败时返回 NULL（调用方应回退到 mini）。
const NJS_IntlBackend* NJS_IcuIntlBackend(void);

// 三级回退选择：custom 非 NULL 时原样返回；否则返回 NJS_IcuIntlBackend()
// （可能为 NULL，此时 createContext 会回退到内置最小后端）。
const NJS_IntlBackend* NJS_SelectIntlBackend(const NJS_IntlBackend* custom);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NEOJS_INTL_ICU_H
