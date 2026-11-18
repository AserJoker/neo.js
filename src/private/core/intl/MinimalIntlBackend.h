// intl/MinimalIntlBackend.h - core 内置的最小 Intl 后端
//
// 无宿主后端时使用：en / latn / gregory / UTC，确定性输出。
// 只实现 Intl.NumberFormat 的 decimal/percent/currency 基本路径；
// 其他能力通过 capabilities 位图声明为不支持。
//
// 关联 ADR: 020（标准库组织）

#ifndef NEO_CORE_PRIVATE_INTL_MINIMAL_INTL_BACKEND_H
#define NEO_CORE_PRIVATE_INTL_MINIMAL_INTL_BACKEND_H

#include "neojs.h"

namespace neo::core {

// 返回内置最小后端的静态实例（生命周期与进程一致）。
const NJS_IntlBackend* minimalIntlBackend();

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_INTL_MINIMAL_INTL_BACKEND_H
