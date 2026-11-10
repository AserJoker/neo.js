# ADR-045: 协程抽象（Coroutine）— 通用挂起/恢复，统一 native 与 VM

- 状态: 提议中（设计稿，未实现）
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-017（Interrupt）、ADR-022（函数调用）、
          ADR-023（协程挂起/恢复）、ADR-036（闭包作用域孤立化）、
          ADR-037（调用栈）、ADR-042（任务实体动态根）、ADR-044（可达性标记）

## 背景

ADR-023 把挂起/恢复描述为「VM 保存现场 + 外部压栈 + 恢复 PC」，这是 **VM 视角**
的实现。但挂起/恢复不是 VM 独有的能力：

- native 函数同样可以「分阶段执行」——C++ 局部变量虽然会随调用返回丢失，
  但把状态放进作用域树（或协程对象）后，重新进入函数即可从断点继续。
- 未来 VM 只是把「断点」从「函数内阶段号」换成「PC 地址」，其余语义完全一致。

因此需要一个**与执行后端无关**的协程抽象：协程 = 可挂起、可恢复的计算单元，
它记录上下文（调用栈 + 作用域树）与断点，函数返回「断点 + yield 值」表示挂起。

本次目标是 **Generator**（`function*` / `yield`）。`async/await` 不在本次范围——
它只是「Generator + 任务队列在 Promise settle 时自动 next」的语法糖，等 Generator
落地后由上层组合即可，不需要协程层额外支持。

## 决策

### 1. 两个概念：Coroutine（可恢复句柄）与 CoroutineYield（挂起信号）

必须区分开，否则「函数返回一个协程对象」与「函数在此处挂起」无法区分：

| 概念 | 类型 | 作用 |
|------|------|------|
| **Coroutine** | `TypeKind::Coroutine`（对象） | 可恢复的计算句柄：持上下文、断点、状态 |
| **CoroutineYield** | `TypeKind::CoroutineYield`（非对象运行时值） | 函数返回它表示「在此挂起」 |

> 命名说明：用户 demo 中的 `ctx->createCoroutine(stage, value)` 实际语义是
> 「创建挂起信号」。为避免与协程对象混淆，本文记为 `ctx->createCoroutineYield(stage, value)`。
> 若坚持沿用 `createCoroutine`，建议至少让它在语义上只用于「函数返回值」这一处。

为什么信号必须是独立类型：若复用 Coroutine 对象类型，函数正常 `return someCoroutine`
会被误判为挂起。CoroutineYield 与 Exception/Fatal 同属「运行时特殊值」（ADR-017），
不参与普通对象语义。

### 2. Coroutine 对象（`CoroutineData : ObjectData`）

命名属性/符号属性/原型链复用对象模型；内部槽用专用字段（与 Promise/Iterator 一致）：

| 字段 | 语义 |
|------|------|
| `CoroutineState state` | `Suspended` / `Running` / `Completed` |
| `uint64_t resumePoint` | 断点：native = 阶段号（stage）；VM = PC |
| `Value* yielded` | 最近一次 yield 的值（Completed 后为最终返回值） |
| `Value* sentValue` | 本次 `next(v)` 传入的值（首次运行/无参为 undefined）。throw 注入即传入 Exception 值（§8.1），协程层始终持有、不主动丢弃 |
| `Value* target` | 被执行的函数对象（native 函数或未来的 VM 函数） |
| `Value* receiver` | 执行时的 this |
| `std::vector<Value*> initialArgs` | 首次运行的实参 |
| `Scope* execScope` | 保存的执行作用域子树根（挂起期间孤立；首次运行前为 nullptr） |
| `std::vector<CallFrame> savedCallStack` | 挂起时的调用栈快照（ADR-037） |
| `CoroutineRunner resume` | 恢复驱动（多态入口，见 §4） |
| `Value* vmState` | 未来 VM 的挂起状态（native 恒为 nullptr） |

状态机：

```
            next() 首次
  Suspended ──────────────► Running
      ▲                        │
      │  yield（返回信号）      │
      └────────────────────────┤
                               │ 正常返回 / 抛异常 / return()
                               ▼
                           Completed
```

### 3. 挂起协议（native 视角）

协程函数（native 状态机）每次被调用都**从头进入**，通过读取当前协程的断点做
`switch` 分派。C++ 局部变量会丢失，**跨 yield 的状态必须放进作用域树或协程对象**。

```cpp
Value* co_demo(IContext* ctx, Value* /*self*/, size_t /*argc*/, Value* /*argv*/[]) {
    CoroutineData* co = ctx->getCurrentCoroutine();
    uint64_t stage = co->resumePoint();      // demo 中的 getInterruptOffset()

    switch (stage) {
        case 0: {
            // ... 首次执行到第一个 yield 之前的逻辑 ...
            return ctx->createCoroutineYield(1, ctx->createUndefined());  // yield;
        }
        case 1: {
            Value* sent = co->sentValue();    // demo 中的 getValue()：next(v) 的 v
            // ... 使用 sent ...
            return ctx->createCoroutineYield(2, ctx->createString("test"));  // yield "test";
        }
        case 2:
            // ... 继续 ...
            break;
    }
    return ctx->createNumber(123);            // 正常返回 → Completed，结果 123
}
```

协议约定：

- 返回 **CoroutineYield** → 挂起：`resumePoint = signal.nextStage`，
  `yielded = signal.value`，保存上下文，把 `yielded` 交给 `next()`。
- 返回**其他任何值**（含 Exception/Fatal）→ 完成：`state = Completed`，
  `yielded = 返回值`，销毁保存的作用域，`next()` 返回 `{ value, done: true }`。
- `resumePoint` 的**含义由 runner 解释**：native 是阶段号，VM 是 PC。
  抽象层只把它当不透明整数。

### 4. 恢复协议与多态（参考 Task::run）

Task 用 `TaskRunner run = Value* (*)(IContext*, Value*)` 承载「怎么执行」的多态。
协程同理：

```cpp
using CoroutineRunner = Value* (*)(IContext* ctx, Value* coroutine, Value* sentValue);
```

`CoroutineData::resume` 在**创建协程时**按 target 种类选择：

| target | runner | resumePoint 语义 |
|--------|--------|------------------|
| native 函数 | `resumeNativeCoroutine` | 阶段号 |
| VM 函数（未来） | `resumeVmCoroutine` | PC |

Generator 内建（`next/return/throw`）只调 `resume`，不关心底层是 native 还是 VM。
这正是「不为具体任务泛化机制，把逻辑放进多态入口」的延续（ADR-043 §5）。

### 5. 上下文保存：作用域树 + 调用栈

这是整个设计的核心难点。现有 `invokeNative`（`Function.cpp`）在返回时**必然**
销毁临时 `execScope`：

```
execScope → closureScope → rootScope → global   （查找链）
```

挂起时不能销毁它，否则跨 yield 的变量全部丢失。因此 `invokeNative` 需要一个
**挂起分支**：

**挂起（返回 CoroutineYield）**

1. 不 pop `execScope`；
2. `execScope->reparent(nullptr)` 从 `closureScope` 摘下（变成孤立子树）；
3. `coroutine->execScope = execScope`；
4. `coroutine->savedCallStack = ctx->callStack()`；
5. 撤销 `closureScope` 的临时挂载（`reparent(nullptr)`）、恢复调用方 root/current；
6. 返回 `signal.value` 给 `next()`。

**恢复（runner 再次进入 native）**

1. `ctx->setRootScope(d->rootScope)`，`closureScope->reparent(d->rootScope)`；
2. 若 `coroutine->execScope == nullptr` → 首次运行，`pushNewScope(closureScope)`；
   否则把保存的 `execScope->reparent(closureScope)` 重新挂上并设为 current；
3. `ctx->setCallStack(coroutine->savedCallStack)`；
4. 把 `sentValue` 写入协程、把协程压入 Context 的当前协程栈；
5. 调 `d->native(...)`，回到 §3 的协议。

> 关键不变式：挂起期间 `execScope` 是**孤立子树**（parent=nullptr），与
> `closureScope` 孤立化（ADR-036）完全同构。恢复时临时重挂，退出时再摘。

### 6. GC 与生命周期

- `CoroutineData::enumerateChildren` 必须登记：`target / receiver / initialArgs /
  yielded / sentValue / vmState`，以及 **`visitScope(execScope)`**。
- `execScope` 是孤立子树，不在 global 树上，只能由协程对象在标记时主动递归
  （与 `FunctionData::closureScope` 同一机制，ADR-044）。协程不可达 → 其作用域树
  与捕获值都不被标记，环整体可回收。
- `~CoroutineData`：`execScope->destroySelf()`（若仍存在）。
- `sentValue`/`yielded` 是普通 Value 字段，由 GC 遍历保活。

### 7. 当前协程栈

`ctx->getCurrentCoroutine()` 返回**最内层**正在恢复的协程。嵌套生成器
（A 的 body 里调用 B.next()）要求 Context 维护一个栈：

```cpp
std::vector<CoroutineData*> coroutineStack_;
```

runner 进入时 push、退出时 pop。`getCurrentCoroutine()` 取栈顶。

### 8. Generator 集成

Generator = Coroutine + 迭代器协议：

- `function*` 创建的函数对象带 `generator` 标记（类似 `constructible`，
  用 `setFunctionGenerator` 显式设置）。`FunctionType::call` 遇到该标记时
  **不执行函数体**，而是创建 Suspended 协程并返回。
- `%GeneratorPrototype%`（原型链：generator 实例 → %GeneratorPrototype% →
  %IteratorPrototype% → Object.prototype）提供：
  - `next(v)`：`resume(coroutine, v)` → `{ value, done }`
  - `return(v)`：见 §8.2（完整 finally 语义待 Interrupt；当前为强制完成）
  - `throw(e)`：见 §8.1（= `resume(coroutine, createException(e))`）
  - `@@iterator` 返回 this；`@@toStringTag` = "Generator"
- 首次 `next()` 前状态为 Suspended、`resumePoint = 0`、`execScope = nullptr`。

### 8.1 throw 注入 = next(createException(v))，无需额外机制

`throw(e)` **不是** C++ 层异常，也不是新的协议。它的本质就是
`next(createException(e))`：把「异常值」作为 `sentValue` 送进协程。

- 协程层完全无感知：`sentValue` 只是普通 Value，`resumePoint` 照常推进。
- 函数体在恢复后检查 `sentValue` 是否为 Exception（`detail::isErrorValue`），
  是则按异常语义处理（通常是直接 `return sentValue` 让它沿返回通道传播，
  或就地 catch 后继续）。
- 因此 §3 的 native 状态机里，每个 `case` 开头加一次检查即可：

```cpp
case 1: {
    Value* sent = co->sentValue();
    if (detail::isErrorValue(sent)) return sent;   // throw 注入：沿异常通道返回
    // ... 正常逻辑 ...
}
```

> 约定：Generator 层把 `throw(e)` 实现为「置 pendingThrow 后调 `resume(co, exception)`」，
> 或者直接 `resume(co, ctx->createException(e))`——两者等价，后者更简单。
> 协程层不新增字段。

### 8.2 return 注入需要 Return 标记值（待补）

`return(v)` 比 `throw(e)` 难：它要求**立即终止函数体**并返回 `v`，
而 native 状态机无法从 C++ 中途跳出（除非用异常，但那是 C++ 异常，与 ADR-006
的异常即值模型冲突）。

根本原因：目前**没有「return」这种运行时标记值**。`throw` 能复用 Exception，
`return` 没有对应物。方案：

- 新增 `TypeKind::Interrupt`（ADR-017 已规划但未实现），携带子类型
  `Return`（未来还可承载 VM 的 `ret/await/yield` 控制信号）。
- `Generator.prototype.return(v)` 置 `coroutine->pendingReturn = v` 后恢复；
  函数体在每个 `case` 开头检查 `ctx->coroutineCheckPendingReturn()`，
  命中则立即 `return v`（与 throw 检查同一位置）。
- 协程层新增 `Value* pendingReturn`（或复用 `sentValue` + 一个标志位），
  `enumerateChildren` 登记。

> 在 Interrupt 落地前，`return(v)` 的临时实现：直接销毁 execScope、
> 置 `state = Completed`、返回 `{ value: v, done: true }`，**不恢复函数体**。
> 对「尚未开始 / 已挂起」的协程这已足够；只有「函数体内有 finally 需要执行」
> 的场景需要真正的 Return 注入，留待 Interrupt 实现。

### 9. 已知限制与待定问题

1. **native 状态存放**：跨 yield 的变量必须显式放进作用域树（`ctx->createXxx(name)`
   / `ctx->store`）或协程对象；C++ 局部变量不保留。这是 native 状态机的固有约束，
   不是缺陷。
2. **`resumePoint` 溢出**：native 阶段号用 `uint64_t`，实际不可能溢出。
3. **`sentValue` 的首次语义**：首次 `next(v)` 的 v 按规范被丢弃（ES2024 §27.5.3.2）。
   协程层**始终持有 sentValue、不做主动丢弃**（§8.1 的 throw 注入也依赖这一点）；
   是否忽略由 Generator 层决定。
4. **`return(v)` 的 finally 语义**：依赖 Interrupt/Return 标记值（§8.2），
   在 Interrupt 落地前只做「不恢复函数体」的简化实现。
5. **协程与 Promise 的交互**：不在本 ADR 范围。async/await 由「Generator +
   微任务队列」在上层组合，协程层不感知 Promise。

### 10. 与 ADR-023 的关系

ADR-023 描述的是 **VM 后端的挂起实现**（保存 PC/操作数栈/作用域）。本 ADR 把它
提升为通用抽象：

- ADR-023 的「PC」= 本 ADR 的 `resumePoint`（VM runner 解释）；
- ADR-023 的「操作数栈 + 作用域」= 本 ADR 的 `execScope` + `vmState`；
- ADR-023 的「外部压栈」= 本 ADR 的 `sentValue`（native 通过协程 API 读取，
  VM 通过恢复时压栈消费）。

ADR-023 的 VM 实现因此成为本抽象的一个 runner，而非独立机制。

## 后果

- 优点：挂起/恢复与执行后端解耦；native 与 VM 共用同一套 Generator/迭代器协议；
  上下文保存复用既有的「孤立作用域子树 + 主动标记」机制（ADR-036/044），
  根环问题不会复现；多态入口沿用 Task::run 的成熟模式。
- 代价：`invokeNative` 需要新增挂起分支（不能无条件销毁 execScope）；
  native 状态机必须把跨 yield 状态放进作用域树；`return(v)` 的 finally 语义
  依赖尚未实现的 Interrupt/Return 标记值（§8.2）。
- 范围：本 ADR 只做设计。实现顺序建议：
  1. `TypeKind::Coroutine` + `CoroutineData` + `CoroutineYield`；
  2. `ctx->createCoroutineYield` / `getCurrentCoroutine` / 协程栈；
  3. `invokeNative` 挂起分支 + `resumeNativeCoroutine`；
  4. Generator 函数标记 + `%GeneratorPrototype%` + next/throw/return；
  5. （后续）Interrupt/Return 标记值（完整 finally 语义）、VM runner、async/await 组合。
