// types/MessagePort.h - MessagePort 实例
//
// 普通对象布局 + 内部槽：
//   [[Endpoint]] → 跨线程端口端点（共享通道）
//   [[Wake]]     → 本 owner 的唤醒信息（析构时失效）
//
// detached=true 表示该对象已被转移，方法应拒绝操作；端点本身由接收方持有。

#ifndef NEO_CORE_PRIVATE_TYPES_MESSAGEPORT_H
#define NEO_CORE_PRIVATE_TYPES_MESSAGEPORT_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <memory>

namespace neo::worker {
class MessagePortEndpoint;
struct PortWake;
}  // namespace neo::worker

namespace neo::core {

class Value;

class MessagePortType : public ObjectType {
public:
    MessagePortType() : ObjectType(TypeKind::MessagePort) {}
    const char* name() const override { return "MessagePort"; }
};

class MessagePortData : public ObjectData {
public:
    std::shared_ptr<worker::MessagePortEndpoint> endpoint;
    std::shared_ptr<worker::PortWake> wake;
    bool detached = false;

    ~MessagePortData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

MessagePortData* asMessagePort(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_MESSAGEPORT_H
