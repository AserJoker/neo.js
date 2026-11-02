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
- **Exception**：异常发生时的错误值类型。
- **Interrupt**：VM 内部发生「打断」的哨兵值。

### 2. Interrupt（VM 内部控制信号）

- **Interrupt 是带子类型 + 挂起状态**：
  - 子类型：**ret / throw / await / yield**。
  - 挂起状态：await/yield 时携带挂起状态（PC/栈/作用域），用于挂起恢复。
- VM 执行到控制流指令时产生 Interrupt，表示「当前函数执行被打断」。

### 3. Exception（业务错误，对外可见）

- **Exception 是业务抛出的错误**，对调用方可见，携带错误对象。

### 4. Exception 与 Interrupt 独立

- **两者独立**：
  - Interrupt 是 VM **内部控制信号**。
  - Exception 是**业务抛出错误**（对外）。

### 5. Throw 的处理流程

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
