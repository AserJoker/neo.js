# ADR-042: 任务实体作为动态 GC 根 — 不挂 scope，由 Context 直管 scopeRef

- 状态: 已接受 (2026-11-09)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-012（Scope 树 + scopeRef）、ADR-029（生命周期）、ADR-036（闭包作用域孤立化）

## 背景

事件循环把任务实体（`Task::entity`）作为 GC 根，以便任务未执行前不被回收。
原实现通过 `global()->add(entity)` 把实体挂进 global scope 的 values 列表。

问题：
- 实体被塞进一个与它无关的 scope，污染 global 的 values（诊断/统计失真）；
- 语义上实体并不属于 global 作用域，只是「被调度器临时持有」；
- 依赖 `Scope::remove` 做线性查找，且解除根的时机分散在出队/取消两处。

## 决策

### 1. 实体是**动态根**，由 Context 直接管理 scopeRef

```cpp
void Context::retainTaskRoot(Value* entity) {
    size_t& refs = taskRootRefs_[entity];
    if (refs == 0) entity->scopeRefInc();   // 首个任务：成为 GC 根
    ++refs;
}

void Context::releaseTaskRoot(Value* entity) {
    auto it = taskRootRefs_.find(entity);
    if (it == taskRootRefs_.end()) return;
    if (--it->second == 0) {
        taskRootRefs_.erase(it);
        entity->scopeRefDec();              // 最后一个任务出队/取消：解除根
    }
}
```

- 实体**不加入任何 Scope 的 values**，只改 `Value::scopeRef`。
- 同一实体被多个任务引用时只 `scopeRefInc` 一次（`taskRootRefs_` 计引用数），
  最后一个任务出队/取消时才 `scopeRefDec`。
- `Scope::remove` 因此不再被事件循环使用（保留为公开 API）。

### 2. 析构时统一归还（关键边界）

`~Context` 必须先 `releaseAllTaskRoots()`：

```cpp
void Context::releaseAllTaskRoots() {
    for (auto& [entity, refs] : taskRootRefs_) {
        for (size_t i = 0; i < refs; ++i) entity->scopeRefDec();
    }
    taskRootRefs_.clear();
    microtasks_.clear();
    macrotasks_.clear();
}
```

原因：队列里未执行的任务不会再出队，若不在析构时归还它们的 `scopeRef`，
这些 Value 会一直保持 `scopeRef > 0`，GC 最终 collect 时判为泄漏并 abort。

顺序很关键：**先归还任务根，再销毁 global scope，最后销毁 GC**。

## 后果

- 优点：实体生命周期语义清晰（调度器持有，而非 global 作用域持有）；global 的
  values 不再被任务污染；解除根只有一处（`releaseTaskRoot`）；析构边界显式处理。
- 缺点：`scopeRef` 不再严格等于「被几个 scope 跟踪」，多了一类「调度器持有」的
  动态根。ADR-012 §4 的表述需理解为「scopeRef 记录外部强引用数」。
- 约束：任何直接 `scopeRefInc` 的动态根都必须在拥有者析构时归还，否则被 GC 判泄漏。
