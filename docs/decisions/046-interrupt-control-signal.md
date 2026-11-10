# ADR-046: Interrupt — 统一控制信号值（kind / offset / value）

- 状态: 提议中（设计稿，未实现）
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-006（异常即值）、ADR-017（Interrupt 规划）、
          ADR-022（函数调用）、ADR-023（协程挂起/恢复）、ADR-045（协程抽象）

## 背景

ADR-045 为协程挂起定义了 `CoroutineYield`（stage + value）。但挂起/恢复只是控制信号的
一种；Generator 的 `return(v)` / `throw(e)`、未来 VM 的 `await`、以及 VM 内部的
`throw` 信号，都是「打断当前执行、携带一个值、并指明从哪继续」的同一类东西。

为每种信号单独造一个类型会散成多个特例。**Interrupt 是这类信号的统一载体**。

关键定位（与 ADR-017 的规划一致，但收窄为「值类型」）：

- Interrupt **不是** Exception，也**不是** Fatal——它与错误无关，是**控制流信号**。
- Interrupt **不参与** `isErrorValue` 判定，不进入 try/catch 语义。
- Interrupt 走与 Exception/Fatal 相同的**统一 return 通道**（ADR-006），但**消费点不同**：
  Exception 由 try/catch 消费，Fatal 由最外层宿主消费，Interrupt 由**协程边界**消费。

## 决策

### 1. 三部分：kind / offset / value

```cpp
enum class InterruptKind : uint8_t {
    Yield,   // 挂起并产出 value（函数返回方向）
    Return,  // 立即以 value 返回（外部注入方向；承载 finally 语义）
    Await,   // 未来：等待 Promise 结果（VM 方向）
    Throw,   // 未来：VM 内部 throw 信号，解包为 Exception（ADR-017 §6）
};

struct InterruptData : public IData {
    InterruptKind kind;
    uint64_t offset;   // 断点：native = 阶段号（stage）；VM = PC
    Value* value;      // 载荷（yield 值 / return 值 / await 结果 / 错误值）
};
```

三部分的职责：

| 字段 | 语义 |
|------|------|
| `kind` | 控制信号种类，决定消费方如何处理 |
| `offset` | 断点位置。抽象层视为**不透明整数**，由 runner 解释（native=stage，VM=PC） |
| `value` | 载荷。Yield 的产出值、Return 的返回值、Await 的结果、Throw 的错误值 |

### 2. offset 的双向语义

Interrupt 有两个使用方向，offset 在两侧含义不同但都是「断点」：

| 方向 | 典型 kind | offset 含义 |
|------|-----------|-------------|
| **函数返回**（协程内 → 外部） | `Yield` | **恢复后应从哪个位置继续**（nextStage / nextPC） |
| **外部注入**（外部 → 协程内） | `Return` / `Throw` | **当前所在位置**（currentStage / currentPC） |

对 `Generator.return(v)`：外部构造 `Interrupt(Return, co->resumePoint, v)` 作为
`sentValue` 注入。offset 记录**当前 stage**——native 函数体恢复后据此定位到
「正在哪个 case 上被要求返回」，VM 则据此定位 PC 以决定是否执行 finally。
这正是「offset 就是当前 stage」的含义。

> 为什么两个方向都带 offset：native 侧它主要用于诊断与对称性；VM 侧它是恢复
> 定位的必需信息（finally 需要知道从哪块代码开始执行清理）。

### 3. 传播：统一 return 通道 + 透传纪律

Interrupt 与 Fatal 一样，是「调用方不应处理、必须原样透传」的值：

- **任何返回 `Value*` 的接口**，若结果是 Interrupt 且当前不在协程边界，
  必须立即 `return` 透传，不转换、不包装、不参与运算。
- **唯一消费点**是协程 runner（`resumeNativeCoroutine` / `resumeVmCoroutine`）。
- 优先级：**Fatal > Interrupt > Exception**。若 Interrupt 的 `value` 本身是 Fatal，
  以 Fatal 为准（理论上不应构造这种值，但透传路径须防御）。

判定辅助（与 `isErrorValue` 并列，互不包含）：

```cpp
inline bool isInterruptValue(Value* v) {
    return v != nullptr && v->type()->kind() == TypeKind::Interrupt;
}
```

### 4. Interrupt 不是对象

`TypeKind::Interrupt` 的 `objectLayout = false`。行为方法与 Exception/Fatal 一致：
全部**透传 self**（不参与运算、属性访问、调用）。它是运行时特殊值，与
Exception/Fatal/Uninitialized 同属一类。

### 5. 与 ADR-045 的关系：CoroutineYield 合并进 Interrupt

ADR-045 的 `CoroutineYield`（stage + value）是 Interrupt 的子集。合并后：

| ADR-045 | ADR-046 |
|---------|---------|
| `ctx->createCoroutineYield(stage, value)` | `ctx->createInterrupt(InterruptKind::Yield, stage, value)` |
| 函数返回 CoroutineYield → 挂起 | 函数返回 Interrupt(Yield) → 挂起 |
| `TypeKind::CoroutineYield` | 取消，统一 `TypeKind::Interrupt` |

**Coroutine 对象本身不变**（仍是可恢复句柄，`TypeKind::Coroutine`）。
只有「挂起信号」从专用类型改为 Interrupt 的一个 kind。

### 6. Generator 三个 API 的映射

| API | 实现 |
|-----|------|
| `next(v)` | `resume(co, v)`（v 是普通值，不是 Interrupt） |
| `throw(e)` | `resume(co, ctx->createInterrupt(InterruptKind::Throw, co->resumePoint, ctx->createException(e)))`，或等价地 `resume(co, createException(e))`——两者语义相同，用 Interrupt 包装更统一 |
| `return(v)` | `resume(co, ctx->createInterrupt(InterruptKind::Return, co->resumePoint, v))` |

函数体在每个 `case` 开头检查 `sentValue`：

```cpp
Value* sent = co->sentValue();
if (isInterruptValue(sent)) {
    InterruptData* ir = asInterrupt(sent);
    if (ir->kind == InterruptKind::Return) return ir->value;      // 立即返回
    if (ir->kind == InterruptKind::Throw) return ir->value;       // 沿异常通道返回
}
```

> 若函数体内有 `finally`，Return 注入需要「先执行 finally 再返回」——native 侧
> 由函数体在每个 case 检查并跳转到清理段；VM 侧由 offset 定位 PC。这是
> Interrupt 携带 offset 的价值所在。

### 7. 新增类型的接入清单

按「新增 exotic 类型打通清单」逐项落地（Interrupt 不是对象，跳过对象模型分派）：

1. `TypeKind::Interrupt`（`IType.h`）。
2. `InterruptKind` 枚举 + `InterruptType` + `InterruptData`（`types/Interrupt.h/.cpp`），
   `objectLayout = false`。
3. `Types.cpp`：`getInterruptType()` + `getTypeByKind` 加 case。
4. 行为方法全部透传 self（add/sub/mul/div/toString/call/construct/
   getProperty/setProperty/hasProperty/deleteProperty/defineProperty/
   defineAccessor/getPrototype/setPrototype）。
5. `TypeBehavior.h`：`isInterruptValue` + `asInterrupt`（内部槽访问）。
6. `InterruptData::enumerateChildren` 遍历 `value`；`clone` 引用语义（ref++ 返回 this）。
7. `IContext::createInterrupt(kind, offset, value)`。
8. 透传检查点：协程 runner、`invokeNative` 的返回值处理、Task 执行器
   （Interrupt 逃逸到事件循环属于内部错误，应报 Fatal 或断言）。
9. 测试：构造/字段访问/透传/协程消费/Generator 三 API。

### 8. 与 ADR-017 的差异

ADR-017 把 Interrupt 描述为「VM 内部信号，子类型 ret/throw/await/yield，携带挂起状态」。
本 ADR 收窄并具体化：

- **收窄**：Interrupt 是**值类型**（三字段），不是「VM 内部状态对象」；挂起状态
  （作用域树、调用栈）由 **Coroutine 对象**持有（ADR-045），Interrupt 只带断点 offset。
- **具体化**：`kind` 枚举化；`offset` 统一 native stage / VM PC；`value` 是载荷。
- **保留**：ADR-017 §6「VM throw 产生 Interrupt(throw) 再解包为 Exception」的流程，
  对应 `InterruptKind::Throw`，由 VM runner 消费并转换为 Exception。

## 后果

- 优点：挂起/恢复、return/throw 注入、未来 await 共用一套信号载体；offset 统一
  native stage 与 VM PC；与 Exception/Fatal 正交，语义清晰；新增类型成本低
  （非对象，透传语义）。
- 代价：多一个「必须透传」的值类型，所有返回 `Value*` 的调用点理论上都要检查——
  与 Fatal 透传契约同样的纪律负担。初期可只在协程边界与关键路径检查，
  随 VM 落地逐步收紧。
- 范围：本 ADR 只做设计。实现顺序：
  1. `TypeKind::Interrupt` + `InterruptType/Data` + `createInterrupt` + `isInterruptValue`；
  2. 更新 ADR-045：`CoroutineYield` → `Interrupt(Yield)`；
  3. Coroutine + Generator（ADR-045 实现顺序）；
  4. （后续）VM runner 的 Await/Throw 消费、finally 语义。
