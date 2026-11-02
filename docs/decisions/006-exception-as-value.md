# ADR-006: 异常即值 — 异常/错误走 return 通道

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-005（native 等价性）

## 背景

ADR-004 确立 VM 是「一次任务一个」，ADR-005 确立 native 与脚本方法等价。
本 ADR 定义**异常模型**，使其与上述两个模型统一。

## 推论（负责人提出）

> 异常/错误只是特殊类型的值，也走 return 返回。

## 决策

### 1. 异常是特殊类型的 Value，走 return 通道

- 异常/错误是一个**特殊类型的 Value**（含数据：错误对象等）。
- 函数抛异常时，它只是 `return error_value`。
- 脚本方法：VM 执行 throw → return error 值。
- native 方法：native_fn 返回 error 值。
- **两者走完全相同的 return 通道，无独立异常控制流。**

### 2. 异常随 return 自然传播，无跨 VM 栈展开

- 调用方检查被调函数返回值：
  - 普通值 → 正常继续。
  - error 值 → 短路后续执行，return 同一个 error 值（或跳 catch）。
- 每个 VM 只在**自己内部**处理异常（检查返回值、短路、return error）。
- **没有跨 VM 的异常栈展开（stack unwinding）**，异常处理与普通返回是同一条路径。

### 3. 与 VM 生命周期模型契合

- 每次函数调用一个 VM：异常时该 VM return error，调用方 VM 统一处理。
- 异常传播依赖 return 链，天然契合「每次调用一个 VM」模型。

### 4. 异常值需与普通值区分

- 异常 Value 需要特殊 tag 与普通值区分
  （否则无法区分「返回了一个 Error 对象」与「抛出了异常」）。
- 具体形态属于 Value 体系设计（ADR 待定，暂不深入）。

### 5. 后续设计要点（暂不展开）

- **try/catch**：调用点检查到 error 值时，若当前在 try 区域内 → 跳 catch 并把 error 绑定到 catch 参数；否则 → return error 继续传播。
- **finally**：无论 return 普通值还是 error 值，finally 块必须执行。

## 推论（副产品）

- native 异常处理与脚本完全统一，无两套机制。
- 调试器可统一展示异常传播路径。

## 后果

- 优点：异常/普通返回同一通道、无跨 VM 栈展开、native/脚本完全统一、模型简洁。
- 缺点：调用点需要检查返回值（脚本由 VM 自动完成，native 需作者处理或封装）。
