// MessageClone.h (private) - JS Value ↔ MessageValue 深拷贝
//
// 关联 ADR: 006（异常即值）、028（infra）

#ifndef NEO_WORKER_PRIVATE_MESSAGECLONE_H
#define NEO_WORKER_PRIVATE_MESSAGECLONE_H

#include "core/IContext.h"
#include "core/Value.h"
#include "worker/MessageValue.h"

#include <vector>

namespace neo::worker {

// 解析 transfer 列表（支持 ArrayBuffer / MessagePort）。重复项/已分离/
// 不支持的类型返回 Exception（DataCloneError）。成功返回 nullptr。
core::Value* resolveTransferList(core::IContext* ctx, core::Value* list,
                                 std::vector<core::Value*>& out);

// 克隆成功后分离 transfer 中的 ArrayBuffer 与 MessagePort。
void applyTransfer(const std::vector<core::Value*>& transfer);

// 把 JS Value 深拷贝为 MessageValue。
// transfer 中的 ArrayBuffer 不复制内存，而是把字节移入消息并在成功克隆后
// 由调用方分离源对象（transferred 收集实际被转移的 buffer）。
// 成功返回 nullptr；不支持的类型返回 Exception（DataCloneError）。
core::Value* valueToMessage(core::IContext* ctx, core::Value* value,
                            MessageValue& out,
                            const std::vector<core::Value*>& transfer = {},
                            std::vector<core::Value*>* transferred = nullptr);

// 把 MessageValue 还原为当前 Context 的 JS Value。
// 成功返回新 Value；OOM/异常返回错误值（调用方透传）。
core::Value* messageToValue(core::IContext* ctx, const MessageValue& value);

}  // namespace neo::worker

#endif  // NEO_WORKER_PRIVATE_MESSAGECLONE_H
