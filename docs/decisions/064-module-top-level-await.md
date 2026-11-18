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
- 挂起时把 `VM::snapshot()` 存入 `co->vmState`，完成时标记模块状态。

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

模块新增状态：

```text
evaluating          求值中（含异步挂起），循环依赖保护
evaluationPromise   异步求值 Promise
evaluationError     失败错误（缓存，不重试）
evaluationCoroutine 求值协程
evaluationFailed    已失败
cycleWaiting        循环依赖方在本模块求值中访问过它
```

失败被缓存：再次 `importModule` 直接返回 `evaluationError`。

### 3. 依赖与传播

`evaluateModule` 先按 DFS 后序求值依赖：

- 依赖同步完成 → 继续；
- 依赖返回 Promise → 收集；
- 依赖失败 → 当前模块失败。

若存在异步依赖，模块进入异步求值：创建 capability 与求值协程，用
`Promise.all(depPromises).then(onFulfilled, onRejected)` 等待全部依赖：

- fulfilled → 首次 resume 协程，执行模块体；
- rejected → 拒绝模块 Promise，缓存错误并标记 `evaluationFailed`。

模块体自身在 `await` 处挂起时，`driveAsyncCoroutine` 订阅 awaitable；
恢复时正常值压栈，拒绝以 `Interrupt(Throw)` 注入挂起点，由模块体的
`try/catch/finally` 正常处理。

### 4. import 语义

- 静态 `import`（`importModule`）是链接期语义：编译模块返回命名空间对象，
  即使求值仍在进行。导出槽位是活绑定，读取未初始化槽位仍抛 TDZ。
- 动态 `import()`（`importModuleDynamic`）返回的 Promise 必须等待模块求值
  完成后才 fulfill 命名空间；模块求值拒绝则 reject 同一原因。
- 原生 loader 模块保持同步惰性加载，不参与异步求值。

### 5. 已知范围外

顶层 await 与**循环依赖**组合需要规范的 async parent / pending dependency
算法。本实现检测到「循环依赖方访问过该模块，且该模块随后变为异步」时，
显式返回 TypeError（`top-level await in a cyclic module graph is not implemented`），
而不是给出错误的执行顺序。

## 后果

- 模块求值从同步函数变为「同步完成 / 异步 Promise」二态，调用方需按三态处理。
- 动态 import 与依赖传播使用现有 Promise 微任务机制，不引入新调度器。
- 协程对象与 VM 快照复用 ADR-045/046 的既有 GC 与恢复机制。
- 附带修复：`Promise.resolve` / `Promise.reject` 允许省略实参
  （`minArgc=0`，`length` 仍为 1）。

## 实现状态

- `ModuleEntry.h`：异步求值状态字段。
- `Module.cpp`：`createModuleCoroutine`、`resumeModuleCoroutine`、
  `startModuleBody`、`subscribeModuleAwait`、`startAsyncModuleEvaluation`、
  三态 `evaluateModule`、动态 import 等待求值 Promise。
- `AsyncNative`：导出 `driveAsyncCoroutine` 供模块协程复用。
- `ModuleCompilerTest`：顶层 await 挂起、向 import 方传播、拒绝缓存、
  动态 import 等待。
