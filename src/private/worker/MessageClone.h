// MessageClone.h (private) - JS Value ↔ MessageValue 深拷贝
//
// 关联 ADR: 006（异常即值）、028（infra）

#ifndef NEO_WORKER_PRIVATE_MESSAGECLONE_H
#define NEO_WORKER_PRIVATE_MESSAGECLONE_H

#include "core/IContext.h"
#include "core/Value.h"
#include "worker/MessageValue.h"

namespace neo::worker {

// 把 JS Value 深拷贝为 MessageValue。
// 成功返回 nullptr；不支持的类型或循环引用返回 Exception（DataCloneError）。
core::Value* valueToMessage(core::IContext* ctx, core::Value* value,
                            MessageValue& out);

// 把 MessageValue 还原为当前 Context 的 JS Value。
// 成功返回新 Value；OOM/异常返回错误值（调用方透传）。
core::Value* messageToValue(core::IContext* ctx, const MessageValue& value);

}  // namespace neo::worker

#endif  // NEO_WORKER_PRIVATE_MESSAGECLONE_H
