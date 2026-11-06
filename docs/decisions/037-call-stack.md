# ADR-037: 调用栈 — 纯展示信息 + 两帧 push + trace 快照

- 状态: 已接受 (2026-11-06)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-017（Interrupt）、ADR-022（函数调用）、ADR-023（协程挂起/恢复）

## 背景

变量解析完全由 Scope 的 parent 链完成，运行时不需要调用栈来定位变量。但错误报告
（`Error.stack`）、调试器、宿主诊断都需要「谁调用了谁、在哪一行」的展示信息。

## 决策

### 1. 调用栈是纯展示信息

- Context 持一个独立的 `std::vector<CallFrame>`，与 Scope 链**互不影响**。
- 不参与任何语义：变量查找、作用域切换、GC 根判定都与它无关。
- 内存走宿主堆（`std::string`），不经 IAllocator——诊断信息不应在 OOM 时因
  Engine 配额耗尽而无法记录。

### 2. 位置用三字段，不预拼接

```cpp
struct SourcePosition {
    std::string filename;  // 非源码帧为 "<internal>"
    uint32_t line = 0;     // 1-based；0 = 未知
    uint32_t column = 0;   // 1-based；0 = 未知
    static SourcePosition internal();
    bool isInternal() const;
};

struct CallFrame {
    std::string name;         // 函数名（"foo" / "<anonymous>"）
    SourcePosition position;  // 该函数内的位置；filename 空 = 未确定
    static CallFrame internal(std::string fnName);
};
```

- 三个字段分开存而非预拼接 `"file:line:col"`：调试器做断点匹配、源码跳转时需要
  结构化数据；字符串拼接只在最终打印时做。
- 非源码帧（native 内建、引擎内部逻辑）：`filename = "<internal>"`，line/column = 0。

### 3. push 必须显式，且跨两帧

**只有 VM 知道当前源码位置**，Context 与 Function 都不知道。因此不能在
`call`/`invoke` 内部自动插入栈帧——除非 call 强制要求传入源码位置参数，
那会把调试信息污染进调用协议。故 push/pop 由调用点显式维护。

调用点的参数是「**在某处调用某函数**」，而栈帧要表达「**在某个函数的某处**」，
两者差一层。因此：

```cpp
pushFrame(callSite, fnName):
    1. 若栈非空：把 callSite 写入当前栈顶帧的 position（调用者的「某处」）
    2. 压入新帧 { name = fnName, position = {} }（新帧位置待定）
```

- 栈空时（最外层调用）没有调用者，只有第 2 步。
- 新帧的 `position` 留空，等它自己再调用别人（被第 1 步写入）或出错（被 trace 补全）。

### 4. trace：clone + 补全栈顶

```cpp
trace(callSite) -> std::vector<CallFrame>:
    clone 当前栈，若非空则把 callSite 写入副本的栈顶帧 position
```

- 用于出错点/挂起点：栈顶帧此刻的位置只有调用方知道。
- **不改动** Context 的栈——快照可安全交给异常值、async 状态或生成器持有。

### 5. 其余接口

```cpp
virtual void popFrame() = 0;                                  // 栈空时无操作
virtual const std::vector<CallFrame>& callStack() const = 0;  // 自底向上，栈顶在末尾
virtual void setCallStack(std::vector<CallFrame> stack) = 0;  // 整体替换
```

- `setCallStack` 供异常跨栈传播、async 恢复、生成器挂起后恢复时整体写回（ADR-023）。

## 后果

- 优点：帧信息自洽（每帧描述自己的位置），错误栈可按 `at name (file:line:col)`
  逐帧打印；结构化位置便于调试器集成；trace 快照不可变，天然适配协程。
- 缺点：调用点需成对 push/pop（漏 pop 只影响展示信息，不影响正确性）；
  当前无前端，`position` 的实际填充要等 VM 阶段。
