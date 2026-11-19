// types/Worker.h - JS Worker 实例（HTML Worker 门面）
//
// 普通对象布局 + 内部槽：
//   [[Worker]]  → 原生 worker 句柄（独立线程 + Context + 子分配器）
//   [[State]]   → 主线程与 worker 线程共享的存活标记/回投上下文
//
// WorkerData 析构时 requestStop + join，保证 worker 线程先退出；state->alive
// 先置 false，使已排队的外部任务不再访问已销毁的 Worker 对象。

#ifndef NEO_CORE_PRIVATE_TYPES_WORKER_H
#define NEO_CORE_PRIVATE_TYPES_WORKER_H

#include "core/IData.h"
#include "core/IType.h"
#include "core/types/Object.h"
#include "infra/IAllocator.h"

#include <atomic>
#include <memory>
#include <string>

namespace neo::worker {
class IWorker;
}  // namespace neo::worker

namespace neo::core {

class IContext;
class Value;

// 主线程与 worker 线程共享的 Worker 存活状态。
// workerObject 只在主线程读写（外部任务在 owner 线程执行）。
struct WorkerState {
    std::atomic<bool> alive {true};
    IContext* mainCtx = nullptr;
    worker::IWorker* worker = nullptr;
    Value* workerObject = nullptr;
    std::string path;
    std::string source;  // 构造时在主线程读入的脚本源码
    bool module = false; // true = module script（new Worker(path, {type:"module"})）
};

class WorkerType : public ObjectType {
public:
    WorkerType() : ObjectType(TypeKind::Worker) {}
    const char* name() const override { return "Worker"; }
};

class WorkerData : public ObjectData {
public:
    std::shared_ptr<WorkerState> state;
    std::unique_ptr<worker::IWorker> worker;

    ~WorkerData() override;

    void enumerateChildren(IGCVisitor* visitor) override;
    IData* clone(neo::infra::IAllocator* alloc) const override;
};

WorkerData* asWorker(Value* v);

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_WORKER_H
