# ADR-017: 类型清单与 Interrupt/Exception

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-006（异常即值）、ADR-016（Type 体系）

## 背景

确定完整的类型清单，以及 Interrupt/Exception 这两个运行时特殊类型。

## 决策

### 1. 类型清单

**正常 JavaScript 类型**：
- undefined / null / boolean / number / string / object / function / symbol / bigint / array / ...

**特殊运行时类型**（除正常 JS 类型外）：
- **Exception**：可捕获异常，业务抛出的错误值类型。
- **Fatal**：不可捕获致命错误，系统级不可恢复故障（如 OOM）。
- **Interrupt**：VM 内部发生「打断」的哨兵值。

### 2. Interrupt（VM 内部控制信号）

- **Interrupt 是带子类型 + 挂起状态**：
  - 子类型：**ret / throw / await / yield**。
  - 挂起状态：await/yield 时携带挂起状态（PC/栈/作用域），用于挂起恢复。
- VM 执行到控制流指令时产生 Interrupt，表示「当前函数执行被打断」。

### 3. Exception（可捕获异常，对外可见）

- **Exception 是业务抛出的错误**，对调用方可见，携带错误对象。
- **可捕获**：脚本 `try/catch` 可捕获处理，native 函数可包装转换。
- 走 ADR-006 异常即值通道，作为带标记的 Value 返回。

### 4. Fatal（不可捕获致命错误，透传到最外层）

- **Fatal 是比 Exception 更严重的错误类型**，表示不可恢复的系统级故障
  （如 OOM、栈溢出等）。
- **不可捕获**：脚本 `try/catch` 无法捕获，**任何返回 `Value*` 的接口**调用方收到
  Fatal Value 后必须**直接透传**，不处理不转换不包装，一路返回到最外层调用方
  （CLI 或嵌入宿主）。
- **透传契约**（适用范围：所有返回 `Value*` 的业务接口——native 函数、VM 执行循环、
  Context 的 `create_*`、Sema/Compiler 内部能产生 Value 的路径，无一例外）：
  - **通用规则**：调用任何返回 `Value*` 的接口后，**必须检查返回值是否为 Fatal**
    （`v->type() == fatal_type()` 或与 `ctx->oom_value()` 指针比较），是则立即
    return 透传，不处理不转换不包装。这是一条无例外的纪律——只要接口签名是
    `Value*`，调用点就必须做 Fatal 检查。
  - **native 函数**：调用其他函数/接口后检查返回值，Fatal 则立即 return 透传。
  - **VM 执行循环**：每条指令的执行结果检查，Fatal 则跳出执行循环，返回给调用方。
  - **最外层处理**：CLI/宿主用**额外内存**（宿主自己的内存管理，不经过 Engine 的
    Allocator；CLI 可用预先创建的栈上空间）解析错误——拼接错误消息、打印调用栈等。
    因 Fatal 发生时 Engine 内存可能已耗尽，不能用 Engine 的 Allocator 分配诊断信息。
    最外层是 Fatal 透传链的**唯一终点**，只有它允许（也必须）处理 Fatal。
- **也是 Value**（Type=Fatal，Data 携带错误码/消息），符合"Engine 层完全用语言特性
  表达错误，返回值必须为 Value"（ADR-030 阶段 5）。
- **与 Exception 的关系**：Exception 是可恢复的业务异常（脚本可处理），Fatal 是不可恢复
  的系统故障（脚本不可处理，宿主兜底）。OOM 属于 Fatal 而非 Exception。

### 5. Exception 与 Interrupt 独立

- **两者独立**：
  - Interrupt 是 VM **内部控制信号**。
  - Exception 是**业务抛出错误**（对外）。
- **Fatal 与两者独立**：Fatal 是系统级故障，非 VM 控制信号也非业务异常。

### 6. Throw 的处理流程

```
脚本执行 throw x
  → VM 内部产生 Interrupt(throw)，携带错误 value x 返回
  → VM 解包 Interrupt，包装成 Exception 对外/对调用方返回
```

## 与既有模型的关系

- **Interrupt 是 VM 内部的控制流机制**：ret/throw/await/yield 都是「打断」。
- **await/yield 的挂起状态**是「挂起恢复」的载体（ADR-004 的 VM 状态保存）。
- **Exception 是对外错误值**，走 return 通道（ADR-006）。

## 后果

- 优点：内部控制（Interrupt）与对外错误（Exception）分离清晰；
  await/yield 挂起状态与异常统一走打断通道。
- 缺点：Interrupt 需携带子类型与挂起状态（结构稍复杂）。
