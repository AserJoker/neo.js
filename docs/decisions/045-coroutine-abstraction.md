# ADR-045: 协程抽象（Coroutine）— 通用挂起/恢复，统一 native 与 VM

- 状态: 已接受（步骤 0-3 已实现：Interrupt、Coroutine 对象与协程栈、挂起分支与 native runner；
  Generator 封装待做）
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

### 1. 两个概念：Coroutine（可恢复句柄）与挂起信号（Interrupt）

必须区分开，否则「函数返回一个协程对象」与「函数在此处挂起」无法区分：

| 概念 | 类型 | 作用 |
|------|------|------|
| **Coroutine** | `TypeKind::Coroutine`（对象） | 可恢复的计算句柄：持上下文、断点、状态 |
| **挂起信号** | `Interrupt(kind=Yield, offset, value)`（非对象运行时值，ADR-046） | 函数返回它表示「在此挂起」 |

> 命名说明：用户 demo 中的 `ctx->createCoroutine(stage, value)` 实际语义是
> 「创建挂起信号」。该信号统一为 **Interrupt**（ADR-046）：
> `ctx->createInterrupt(InterruptKind::Yield, stage, value)`。
> 早期草案的 `CoroutineYield` 专用类型已合并进 Interrupt，不再单独存在。

为什么信号必须是独立类型：若复用 Coroutine 对象类型，函数正常 `return someCoroutine`
会被误判为挂起。Interrupt 与 Exception/Fatal 同属「运行时特殊值」（ADR-017），
不参与普通对象语义；但它与错误无关，是控制流信号（ADR-046）。

### 2. Coroutine 对象（`CoroutineData : ObjectData`）

命名属性/符号属性/原型链复用对象模型；内部槽用专用字段（与 Promise/Iterator 一致）：

| 字段 | 语义 |
|------|------|
| `CoroutineState state` | `Suspended` / `Running` / `Completed` |
| `uint64_t resumePoint` | 断点：native = 阶段号（stage）；VM = PC |
| `Value* yielded` | 最近一次 yield 的值（Completed 后为最终返回值） |
| `Value* sentValue` | 本次恢复传入的值（首次运行/无参为 undefined）。可能是普通值或 Interrupt（throw/return 注入，§8.1）；协程层始终持有、不主动丢弃 |
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
            return ctx->createInterrupt(InterruptKind::Yield, 1,
                                        ctx->createUndefined());  // yield;
        }
        case 1: {
            Value* sent = co->sentValue();    // demo 中的 getValue()：next(v) 的 v
            // ... 使用 sent ...
            return ctx->createInterrupt(InterruptKind::Yield, 2,
                                        ctx->createString("test"));  // yield "test";
        }
        case 2:
            // ... 继续 ...
            break;
    }
    return ctx->createNumber(123);            // 正常返回 → Completed，结果 123
}
```

协议约定：

- 返回 **Interrupt(Yield)** → 挂起：`resumePoint = signal.offset`，
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

这是整个设计的核心难点。现有 `invokeNativeFunction`（`FunctionInvoke.cpp`，原
`Function.cpp` 内的 `invokeNative`）在返回时**必然**销毁临时 `execScope`：

```
execScope → closureScope → rootScope → global   （查找链）
```

挂起时不能销毁它，否则跨 yield 的变量全部丢失。因此 `invokeNativeFunction` 需要一个
**挂起分支**：

**挂起（返回 Interrupt(Yield)）**

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
  - `return(v)`：`resume(coroutine, Interrupt(Return, resumePoint, v))`（§8.1）
  - `throw(e)`：`resume(coroutine, Interrupt(Throw, resumePoint, createException(e)))`（§8.1）
  - `@@iterator` 返回 this；`@@toStringTag` = "Generator"
- 首次 `next()` 前状态为 Suspended、`resumePoint = 0`、`execScope = nullptr`。

### 8.1 throw / return 注入统一走 Interrupt（ADR-046）

Generator 的 `throw(e)` 与 `return(v)` 都是「外部把控制信号注入协程」，
统一用 **Interrupt**（ADR-046）作为 `sentValue`：

| API | 注入的 sentValue |
|-----|------------------|
| `next(v)` | 普通值 `v`（不是 Interrupt） |
| `throw(e)` | `Interrupt(Throw, co->resumePoint, createException(e))` |
| `return(v)` | `Interrupt(Return, co->resumePoint, v)` |

函数体在每个 `case` 开头检查（与读取 sentValue 同一处）：

```cpp
Value* sent = co->sentValue();
if (isInterruptValue(sent)) {
    InterruptData* ir = asInterrupt(sent);
    // Throw / Return 都沿返回通道传出 payload；有 finally 时先跳转清理段
    if (ir->kind == InterruptKind::Throw) return ir->value;   // 异常通道
    if (ir->kind == InterruptKind::Return) return ir->value;  // 正常返回
}
```

要点：

- **throw 注入不需要新机制**：它是 `next(createException(v))` 的等价物，
  协程层只是透传一个值。
- **return 注入的关键是 offset**：Interrupt 携带 `co->resumePoint`（当前 stage），
  native 函数体据此知道「在哪个 case 上被要求返回」，从而决定是否先执行 finally；
  VM 侧据此定位 PC。这是 ADR-046 给 Interrupt 带 offset 的价值。
- 协程层不新增 `pendingThrow` / `pendingReturn` 字段——信号就在 `sentValue` 里。

### 9. 已知限制与待定问题

1. **native 状态存放**：跨 yield 的变量必须显式放进作用域树（`ctx->createXxx(name)`
   / `ctx->store`）或协程对象；C++ 局部变量不保留。这是 native 状态机的固有约束，
   不是缺陷。
2. **`resumePoint` 溢出**：native 阶段号用 `uint64_t`，实际不可能溢出。
3. **`sentValue` 的首次语义**：首次 `next(v)` 的 v 按规范被丢弃（ES2024 §27.5.3.2）。
   协程层**始终持有 sentValue、不做主动丢弃**（§8.1 的 throw 注入也依赖这一点）；
   是否忽略由 Generator 层决定。
4. **`return(v)` 的 finally 语义**：依赖 Interrupt(Return) 的 offset 定位（§8.1 / ADR-046）。
   函数体尚未实现 finally 时，可先按「直接返回 payload」处理。
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
- 代价：`invokeNativeFunction` 需要新增挂起分支（不能无条件销毁 execScope）；
  native 状态机必须把跨 yield 状态放进作用域树；`return(v)` 的 finally 语义
  依赖 Interrupt(Return) 的 offset 定位（§8.1）。
- 范围：本 ADR 只做设计。实现顺序建议：
  0. 先落地 `TypeKind::Interrupt`（ADR-046）；✅
  1. `TypeKind::Coroutine` + `CoroutineData`；✅
  2. `ctx->createInterrupt(Yield, …)` / `getCurrentCoroutine` / 协程栈；✅
  3. `invokeNativeFunction` 挂起分支 + `resumeNativeCoroutine`；✅
  4. Generator 函数标记 + `%GeneratorPrototype%` + next/throw/return；
  5. （后续）finally 语义、VM runner、async/await 组合。

实现落点（步骤 1-3）：
- `src/public/core/Coroutine.h`：`CoroutineState` + `CoroutineRunner`；
- `src/private/core/types/Coroutine.h/.cpp`：`CoroutineType` + `CoroutineData`；
- `src/private/core/FunctionInvoke.cpp`：`invokeNativeFunction` 承载调用流程与挂起分支
  （`FunctionType::call/construct` 与协程 runner 共用）；
- `src/private/core/NativeCoroutine.cpp`：`resumeNativeCoroutine`；
- `IContext::createCoroutine / getCurrentCoroutine / resumeCoroutine`。
