# ADR-044: GC 根改为「从 global 作用域树可达」，根治 scopeRef 根环

- 状态: 已接受 (2026-11-09)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-012（Scope 树）、ADR-015（data 引用计数）、ADR-029（生命周期）、
          ADR-036（闭包作用域孤立化）、ADR-042（任务实体动态根）、ADR-043（Promise）

## 背景

旧模型的 GC 根判据是 **`Value::scopeRef() != 0`**（被任一 Scope 的 `values_` 跟踪）。
标记阶段从所有这样的 Value 出发，沿 `IData::enumerateChildren` 的 data 引用链递归标记。

ADR-036 把闭包作用域孤立化（不再挂 root 下），解决了「每个函数一个永久 GC 根」的
常驻泄漏。但**根环问题依然存在**：

- 闭包作用域 `values_` 中的 Value 是根；
- 若其中某个 Value 沿 data 链（属性、boundArgs、capability…）回到该闭包所属的
  函数对象，则 `scopeRef != 0` 与 data 可达性互相保活，标记-清除永远回收不了。
- 典型形态：具名函数表达式自引用、递归函数名、对象方法引用宿主对象、
  Promise 早期用 `captureValue` 注入目标 promise 的 resolve/reject。

这不是「谁忘了释放」，而是**根判据与 data 图可达性两个机制叠加后的结构性缺陷**。

## 决策

### 1. 根 = 从 global 作用域树可达

`collect` 的标记阶段改为：

1. 重置所有 Value 的 `marked`；
2. 从 **global 作用域树**出发：`visitScope(global)` 递归
   「scope.values_ + scope.children_」，对每个 Value 调 `visit`，
   `visit` 再沿 `data->enumerateChildren` 递归；
3. `pinRef > 0` 的 Value 作为**额外根**（见 §3）；
4. 清除阶段回收未标记的 Value。

`add` / `remove` 不再改变任何引用计数，scope 只是可达性图上的一个节点。

### 2. 闭包作用域由函数对象主动标记

孤立闭包作用域不在 global 树上，因此 `IGCVisitor` 新增：

```cpp
virtual void visitScope(Scope* scope) = 0;
```

`FunctionData::enumerateChildren` 在标记时调用 `visitor->visitScope(closureScope)`。

关键性质：**函数对象不可达时 `enumerateChildren` 根本不会被调用**，其闭包作用域
及捕获值都不被标记。于是「函数 → closureScope → 捕获值 → 函数」的环整体不可达，
可被回收——根环在结构上消失。

`visitScope` 内部用 `unordered_set` 去重，同一作用域被多次请求标记无副作用。

### 3. `scopeRef` 改名 `pinRef`，语义收窄为「手工钉住」

`Value::scopeRefInc/Dec` → `pinRefInc/Dec`，`scopeRef()` → `pinRef()`。
`pinRef > 0` 表示该 Value 是 GC 图之外的强引用，作为额外根被标记。当前用途：

| 使用方 | 说明 |
|--------|------|
| `Task::entity` | 事件循环队列持有，出队/取消时归还（ADR-042） |
| 未处理拒绝的 promise | 待上报期间持有，上报/析构时归还（ADR-043 M4） |

两者都**不挂任何 scope**，由拥有者（Context）直接管理计数，析构时统一归还。

### 4. global 作用域树所有权移交 GC

`IGCEngine` 新增 `setRootScope(Scope*)`。Context 构造时创建 global 后立即
`gc_->setRootScope(global_)`；`~MarkSweepGC` 先 `rootScope_->destroySelf()`
（销毁整棵树），再循环 collect 直到收敛（此时所有 Value 都不可达）。

Context 不再在析构时手工销毁 global；`gc_.reset()` 一步完成「销毁树 + 回收 Value」。

## 后果

- 优点：根环问题结构性根治；根判据与 data 可达性统一为一张图；`pinRef` 语义单一
  且显式（只有调度器用它）；闭包作用域的生命周期完全由函数对象可达性决定。
- 代价：
  - `collect` 需要遍历作用域树（O(scopes + values)），比「扫 values_ 看计数」多一层；
  - 任何「GC 图之外的强引用」都必须显式 `pinRef`，否则会被提前回收——
    这是比旧模型更严格的所有权契约。
- 兼容性：`Scope::add/remove` 不再影响引用计数，相关单测已按新语义改写。

## 验证

新增 `tests/unit/core/GcRootCycleTest.cpp`：

- 自引用闭包（函数捕获自身）在函数不可达后被回收；
- 函数 → closureScope → 对象 → 函数 的环被回收；
- 从 global 树可达的自引用闭包**不**被误回收；
- `pinRef > 0` 的 Value 在脱离作用域树后仍存活；
- 闭包捕获的普通值随函数一并回收。

Windows 1019 单测 + 44 集成、WSL ASan（`detect_leaks=1`）全部通过，无泄漏。
