# ADR-064: 模块顶层 await（Top-Level Await）

- 状态: 已接受 (2026-11-18)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-033（eval）、ADR-043（Promise）、ADR-045（协程）、
  ADR-046（Interrupt）、ADR-059（模块绑定与链接）、ADR-060（VM 框架）

## 背景

Sema 已按 §16.2.1 允许模块顶层 `await`（`Program(module)` 的 async 上下文为真），
Compiler 也会正常发射 `await` 指令。但运行期 `Context::evaluateModule` 仍是同步的：

- `runModuleCode` 直接 `machine.run(evaluatePc)`；
- `opAwait` 产生的 `Interrupt(Await)` 被当作「非错误返回值」忽略；
- 模块随即被标记 `evaluated`，await 之后的语句永远不会执行。

因此需要把模块求值改造成可挂起/恢复的异步过程，并让依赖方与动态 `import()`
等待其完成。

## 决策

### 1. 模块求值复用协程抽象

模块顶层不是函数对象，但同样是「可挂起的 VM 执行」。复用 `CoroutineData`：

- 新增 `CoroutineData::module`（非空表示模块顶层求值协程）与 `started`；
- `Context::createModuleCoroutine(Module*)` 创建协程，`resume` 指向
  `Context::resumeModuleCoroutine`；
- 首次 resume 执行 `machine.run(evaluatePc)`，之后 `machine.resume(vmState, sent)`；
- 挂起时把 `VM::snapshot()` 存入 `co->vmState`；完成/失败由
  `AsyncModuleExecutionFulfilled/Rejected` 统一处理。

模块作用域常驻 global 作用域树，**不做 execScope 孤立化**：VM 栈上的临时值
由模块作用域（及其子块作用域）跟踪，挂起期间仍是 GC 可达的。协程对象、
求值 Promise 与错误值显式 `scope->add(...)` 到模块作用域，保证跨微任务存活。

### 2. 求值结果三态

`Context::evaluateModule(Module*)` 返回值：

```text
nullptr   —— 同步求值成功
Promise   —— 异步求值中（顶层 await 或异步依赖未完成）
错误值     —— 求值失败（Exception/Fatal）
```

模块按 §16.2.1.6.1.7 持有求值状态机：

```text
status                  Linked / Evaluating / EvaluatingAsync / Evaluated
dfsAncestorIndex        DFS 祖先索引，用于 SCC 检测
cycleRoot               所属 SCC 的根（非空）
hasTLA                  [[HasTLA]]：编译期静态标记（见 §5）
asyncEvaluationOrder    [[AsyncEvaluationOrder]]：UNSET / DONE / 全局单调序号
asyncParentModules      [[AsyncParentModules]]：依赖本异步模块的父模块
pendingAsyncDependencies [[PendingAsyncDependencies]]
topLevelPromise/Resolve/Reject  环根的 [[TopLevelCapability]]
evaluationError         失败错误（缓存，不重试）
evaluationCoroutine     求值协程
```

失败被缓存：再次 `importModule` 直接返回 `evaluationError`。

### 3. 依赖与传播：SCC 收缩 + 异步完成 DAG

`innerModuleEvaluation` 按规范实现：

1. 设 `Evaluating`、`dfsAncestorIndex = index`、`pendingAsyncDependencies = 0`，
   入栈后 DFS 求值每个依赖；
2. 依赖仍为 `Evaluating` → 更新 `dfsAncestorIndex`（环）；
3. 依赖已异步/已求值 → 解析为 `cycleRoot`，若其有错误则失败；若
   `asyncEvaluationOrder` 是整数则 `pendingAsyncDependencies++` 并登记到
   `asyncParentModules`；
4. 自身 `hasTLA` 或存在异步依赖 → 分配 `asyncEvaluationOrder`；pending 为 0
   时立即 `ExecuteAsyncModule`；
5. `dfsAncestorIndex == moduleIndex` 时弹栈收缩 SCC：成员共享 `cycleRoot`，
   有异步序号的置 `EvaluatingAsync`，否则置 `Evaluated`。

异步模块完成时：

- `AsyncModuleExecutionFulfilled`：标记 `Evaluated`/`DONE`；环根结算
  `TopLevelCapability`；`GatherAvailableAncestors` 递减父模块 pending，归零者
  进入执行列表，同步父模块继续向上收集；
- 执行列表按 `asyncEvaluationOrder` 排序：有 TLA 的模块启动协程，否则同步
  执行模块体后标记完成并结算其环根 capability；
- `AsyncModuleExecutionRejected`：缓存错误、结算环根、沿
  `asyncParentModules` 递归拒绝。

模块体自身在 `await` 处挂起时，`driveAsyncCoroutine` 订阅 awaitable；
恢复时正常值压栈，拒绝以 `Interrupt(Throw)` 注入挂起点，由模块体的
`try/catch/finally` 正常处理。

### 4. import 语义

- 静态 `import`（`importModule`）是链接期语义：编译模块返回命名空间对象，
  即使求值仍在进行。导出槽位是活绑定，读取未初始化槽位仍抛 TDZ。
- 动态 `import()`（`importModuleDynamic`）返回的 Promise 必须等待模块求值
  完成后才 fulfill 命名空间；模块求值拒绝则 reject 同一原因。
- 原生 loader 模块保持同步惰性加载，不参与异步求值。

### 5. [[HasTLA]] 静态标记

`HasTLA` 必须静态可知：SCC 展开前就要决定成员是 `Evaluated` 还是
`EvaluatingAsync`。Compiler 用 `functionDepth_` 区分模块顶层与嵌套函数：

- 顶层 `await` 表达式、顶层 `for await...of` 置 `Code::hasTopLevelAwait`；
- 嵌套函数内的 await 不计入。

`setModuleCompilation` 在 move 编译产物前把该标记写入 `Module::hasTLA`。

### 6. 循环依赖

循环依赖是合法的 SCC，不再报 `not implemented`。规范不检测「互相 await
值」的死锁，也不把 TLA 环当作 Promise 链自环；环由 SCC 收缩消解，
`AsyncParentModules` 在收缩后构成 DAG，Promise 仅作为完成信号。

## 后果

- 模块求值从同步函数变为「同步完成 / 异步 Promise」二态，调用方需按三态处理。
- 动态 import 与依赖传播使用现有 Promise 微任务机制，不引入新调度器。
- 协程对象与 VM 快照复用 ADR-045/046 的既有 GC 与恢复机制。
- 附带修复：`Promise.resolve` / `Promise.reject` 允许省略实参
  （`minArgc=0`，`length` 仍为 1）。

## 实现状态

- `Code.h` / `Compiler`：`hasTopLevelAwait` 静态标记（`functionDepth_`）。
- `ModuleEntry.h`：`ModuleStatus`、SCC 与异步依赖字段。
- `Module.cpp`：`createModuleCoroutine`、`resumeModuleCoroutine`、
  `innerModuleEvaluation`、`executeAsyncModule`、`executeModuleSync`、
  `gatherAvailableAncestors`、`asyncModuleExecutionFulfilled/Rejected`、
  三态 `evaluateModule`、动态 import 等待环根求值 Promise。
- `AsyncNative`：导出 `driveAsyncCoroutine` 供模块协程复用。
- `ModuleCompilerTest`：顶层 await 挂起、向 import 方传播、拒绝缓存、
  动态 import 等待，以及循环 TLA（同步成员、双 TLA、非根成员、拒绝传播）。
