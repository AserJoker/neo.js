# ADR-060: VM 框架 — 一次逻辑执行的状态机

- 状态: 提议中（框架设计）
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-006（异常即值）、ADR-013（闭包）、
  ADR-022（函数调用）、ADR-023（协程挂起/恢复）、ADR-024（字节码指令集）、
  ADR-037（调用栈）、ADR-046（Interrupt）、ADR-059（模块绑定）

## 背景

Engine 已经具备作用域、绑定、对象模型、函数调用、异常值、协程等原语。
现在需要 VM 把「Sema 产物 / 字节码」和 Engine 原语桥接起来，并用直接构造
字节码的方式做真实可验证的纵向链路。

本 ADR 只定 VM 框架，不追求一次覆盖全部语法。后续按纵向切片逐条打通
AST 节点 → Compiler → Bytecode → VM → Engine。

## 决策

### 1. VM 的粒度：一次逻辑执行一个 VM

一个 VM 对象只负责**一次逻辑执行**：

- 一次函数调用；
- 一次模块顶层执行；
- 一次 `eval` 执行。

VM 不跨执行复用，也不代表整个进程的 CPU。函数 `call` 产生被调函数的
新 VM；调用方 VM 在 `call` 指令处暂停，等待返回值压回操作数栈。

`Context` 仍是持久状态（作用域树、对象、标准库、调用栈展示信息）；
VM 是临时 CPU 状态。**try 栈与 label 栈属于本次执行的控制状态，放在 VM**，
这是对 ADR-004 §1 的细化：Engine 只保留跨执行可见的调用栈展示信息。

### 2. VM 的核心状态

第一步只搭建 pc + 操作数栈模型。try/label 栈**暂不设计结构、不实现**，
等 V3/V4 再引入；引入前 VM 不承担异常跳转与 break/continue 展开。

```cpp
class VM {
    const Code* code_;          // 本次执行的字节码
    uint32_t pc_ = 0;           // 指令下标（不是字节偏移）
    std::vector<Value*> stack_; // 操作数栈（借用，不 pin）

    Scope* entryScope_;         // 本次执行的入口作用域
    Scope* rootScope_;          // 模块根作用域（查找链上界）
    Value* function_;           // 当前 script 函数（模块/eval 可为空）
    size_t argc_ = 0;
    Value** argv_ = nullptr;
    VmFlags flags_;
};
```

第一步的核心结构只有两个：

| 结构 | 职责 |
|------|------|
| `pc_` | 当前指令下标；跳转/挂起恢复的唯一位置 |
| `stack_` | 操作数栈；所有表达式求值经此 |

**操作数栈不 pin Value**：栈上元素都是 Engine 已创建、由当前作用域跟踪的
值（临时变量同样由 `create*` 登记进 current scope）。VM 只借用指针，
不增加 GC 根，也不负责生命周期。返回值离开 VM 时由调用方 `restoreValue`
归属到恢复后的作用域（ADR-022）。

### 3. 字节码表示

```cpp
enum class OpCode : uint16_t { ... };

struct Instruction {
    OpCode op;
    uint32_t a = 0;   // name index / const index / target pc / argc
    uint32_t b = 0;   // 第二操作数（flags / label id 等）
};

struct Code {
    std::vector<Instruction> instructions;
    std::vector<double> numbers;        // 数值常量
    std::vector<std::string> strings;   // 字符串/名字表
    SourceMap sourceMap;                // 源码位置 ↔ 指令范围
};
```

- `pc` 是**指令下标**，不是字节偏移；标签直接解析为指令下标。
- **字符串表由 Compiler 阶段生成**：`push_string <id>` 的操作数是
  `Code::strings` 的下标，字节码里不内联字符串，避免指令变长。
- 数值同理用 `numbers` 表 + `push_number <id>`；VM 执行时再通过 Engine
  创建 Value，自动归属当前作用域，避免 Code 成为 GC 根。
- `Code` 是纯数据，由 Module / script Function 持有；VM 只借用。

### 3.1 SourceMap：源码位置 → 指令范围

异常可能在任何表达式指令上产生，而 Engine 的运算原语不知道当前 pc。
VM 是唯一知道 pc 的地方，因此需要一张「源码位置 → 指令范围」表，让 VM
在指令失败时反查当前源码位置。

```cpp
struct SourceMapEntry {
    uint32_t pcBegin;          // 含
    uint32_t pcEnd;            // 不含
    SourcePosition position;   // 该范围对应的 JS 源码位置
};

class SourceMap {
    std::vector<SourceMapEntry> entries_;  // 按 pcBegin 升序
public:
    void add(uint32_t pcBegin, uint32_t pcEnd, SourcePosition position);
    const SourcePosition* lookup(uint32_t pc) const;
};
```

- **方向是源码 → 字节码**：Compiler 为每个 AST 节点发射指令时，记录该节点
  生成的指令范围 `[pcBegin, pcEnd)` 及其源码位置。
- **反查取最内层**：`lookup(pc)` 找所有 `pcBegin <= pc < pcEnd` 的条目，
  取范围最窄者（嵌套表达式时内层节点覆盖外层语句）；同宽取 `pcBegin` 更大者。
- **覆盖到表达式级**：不能只按 statement 记录，否则 `a + b` 中 `a` 的读取
  失败会定位到整条语句。Compiler 每发射一组表达式指令就登记一次。
- **失败时补全调用栈**：VM 在指令返回 Exception 时调用
  `ctx->setExceptionPosition(exception, currentPosition())`，由 Context 用
  `trace(position)` 重渲染 Error 的 `stack`。这样即使错误由 Engine 原语
  （`load`/`binaryOp` 等）创建、创建时不知道位置，最终栈顶帧也能落在正确的
  `filename:line:column` 上。
- SourceMap 由 Compiler 生成；手工构造 Code 的测试可直接填表验证反查。

### 4. 执行循环与返回协议

```cpp
enum class VmStatus { Returned, Halted, Suspended, Failed };

struct VmResult {
    VmStatus status;
    Value* value;   // Returned/Suspended/Failed 的载荷；Halted 为 undefined
};

VmResult VM::run();
```

- 每条指令执行后检查 Engine 返回的 `Value*`：
  - Exception / Fatal → 先用 `currentPosition()` 补全异常栈，再走 `tryStack_`；
    无处理器则 `Failed` 返回；
  - Interrupt(Yield/Await) → `Suspended` 返回，保存 `VmState`；
  - 正常值 → 继续。
- `ret` → `Returned`；`hlt` → `Halted`。
- 不使用 C++ 异常；错误统一走 Engine 的 `Value*` 返回通道（ADR-006）。

### 5. 调用约定

沿用 ADR-024：

```
[fn, argv0..argvN-1]  call N  →  [ret]
```

- `call N` 从栈上弹出 fn 与 N 个实参，调用 `ctx->invoke(fn, undefined, N, args)`；
- script 函数由 invoke 创建新 VM，native 函数直接执行；
- 返回值压回调用方操作数栈；
- `call` 前后调用点栈平衡：`d → d - N`（弹 N+1 压 1）。

`ret` 前编译器统一补 `push_undefined`，VM 不需要处理“函数体自然结束”特例。

### 6. 作用域与栈平衡

- statement 执行前后操作数栈深度必须一致（ADR-004）。
- VM 记录 `entryScope_`；函数序言通过 `push_scope` 建 body scope。
- 正常 `ret`、异常展开、break/continue 跨作用域时，VM 按 try/label 条目
  记录的 `scopeDepth` 逐层 `popScope`，回到目标深度。
- VM 不拥有作用域；作用域的创建/销毁仍由 Context/Function 生命周期管理。

### 7. try / label（后续，不在第一步）

第一步不设计、不实现 try 栈与 label 栈。V0–V2 只做线性执行与条件跳转；
等 V3/V4 再单独定结构：

- try 栈需要记录 catch/finally 目标、操作数栈深、作用域深；
- label 栈需要记录 break/continue 目标与跨 finally 的 completion；
- 两者都会影响异常展开与作用域回退，必须在有真实语法切片时一起设计。

### 8. 挂起/恢复

- VM 可序列化为 `VmState`：pc、操作数栈、作用域指针、函数/argc/argv。
  后续引入 try/label 后，再把对应栈加入快照。
- `Suspended` 时把 `VmState` 交给 Coroutine；恢复时重建 VM，先压入外部
  注入值（yield/await 结果），再从 `pc` 继续（ADR-023）。
- 嵌套调用挂起时，每一层 VM 都保存自己的 `VmState`，形成调用链；
  Coroutine 持有这条链。同步执行的 V0–V4 不依赖该机制，V5 后再接入。

### 9. 函数对象扩展

`FunctionData` 增加 script 函数所需字段：

```cpp
bool isScript = false;
const Code* code = nullptr;
uint32_t entryPc = 0;
size_t paramCount = 0;
bool hasRest = false;
```

- native 函数保持现状；
- script 函数的 `invoke` 分支创建 VM，从 `entryPc` 开始执行；
- `push_function` 创建 script 函数对象并绑定 `code/entryPc/closureScope`。

### 10. 文件布局

```
src/private/vm/
  OpCode.h        // 指令枚举 + 栈契约注释
  Code.h          // Instruction / Code / SourceMap
  VM.h            // VM（pc/stack）与 VmResult
  VM.cpp          // 执行循环与指令实现
  VmState.h       // 挂起状态（后续接入）
tests/unit/vm/
  VMTest.cpp      // 直接构造 Code 的纵向链路测试
```

## 纵向切片计划

不做“先写完 VM 再写 Compiler”，而是每条切片都从真实语法/AST 节点出发，
打通到 Engine，并保留可独立验证的手工字节码测试。

| 切片 | AST/语法 | VM 能力 | 验证 |
|------|----------|---------|------|
| V0 | 字面量、表达式语句、`return` | push_* / add / load / store / define / ret / hlt | 手工 Code：`1+2`、变量读写 |
| V1 | `var/let/const`、块、if/while | define(flags) / initialize / jmp / jtrue / jfalse / push_scope | 手工 Code + 简单 Compiler |
| V2 | 函数声明/调用、箭头 | push_function / set_closure / call / store_this / load_this | 脚本函数调用 |
| V3 | try/catch/finally、throw | try 栈、completion、异常展开 | 异常路径 |
| V4 | break/continue/标签 | label 栈、跨 finally 跳转 | 循环/标签 |
| V5 | 对象/数组/属性/方法调用 | get_prop/set_prop/call_method | 对象操作 |
| V6 | import/export | 模块三段式、槽位提升、link | 模块端到端 |
| V7 | generator/async/await | VmState 链、Interrupt 消费 | 挂起恢复 |

V0 是第一个可真实运行的 VM→Engine 链路：不依赖 Compiler，直接在测试里
构造 `Code`，验证操作数栈、变量绑定、算术与返回协议。

## 后果

- 优点：VM 状态边界清晰；try/label 栈按执行隔离；操作数栈不引入额外 GC 根；
  每个切片都能独立验证；后续 Compiler 只需发射已打通的指令。
- 缺点：需要扩展 FunctionData 与 invoke 分支；挂起恢复需要 VmState 链，
  在 V5 前只能做同步执行。
- 待定：`Code` 的所有权与跨模块共享、常量池对对象字面量的表示、
  `call_method` 的 self 传递细节、异常跨 VM 的调用栈快照合并。
