// WorkerBuiltins.h - JS Worker 门面注册
//
// 关联 ADR: 020（标准库组织）、006（异常即值）

#ifndef NEO_CORE_PRIVATE_WORKER_BUILTINS_H
#define NEO_CORE_PRIVATE_WORKER_BUILTINS_H

namespace neo::core {

class IContext;

// 注册全局 Worker 构造函数与 Worker.prototype。
bool registerWorkerBuiltins(IContext* ctx);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_WORKER_BUILTINS_H
