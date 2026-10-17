# neo.js 编码规范与代码细节（v1.0）

> 版本：v1.1
> 日期：2026-10-17
> 状态：**待用户确认**
> 适用范围：本项目全部代码（含 C API 层、C++ 内部实现、测试）
> 关联文档：`docs/architecture.md`（架构设计）

---

## 一、项目定位与接口边界

### 1. 定位：嵌入引擎 + C ABI

- **主要应用目标**：作为其它项目的**嵌入引擎**，而非独立运行时。REPL/CLI 仅作为调试工具。
- **对外接口必须使用 C**（稳定 C ABI），保证跨语言/跨工具链的 FFI 绑定。
- **内部实现可以使用 C++**，但对外头文件必须只暴露 C 接口（`extern "C"`），不暴露 C++ 类型。
- 需设计稳定的 C API 层（风格参考 QuickJS/Duktape）。

### 2. API 命名风格（QuickJS 风格但更规范）

- 允许类似 QuickJS 的 API 风格，但**命名必须规范**：禁止 `JS_XXXX` / `JS_XXXX2` 这类带数字后缀的混乱命名。
- **类型命名**：无下划线、PascalCase，如 `JSValue`。
- **方法命名**：携带下划线，`前缀_动作` 形式，如 `JS_CreateNumber`。
- **整体采用面向对象**：C ABI 通过**不透明指针 + 访问方法**实现。
- **C API 函数是 C++ 内部对象方法的薄封装**，`self` 指向内部 C++ 对象，C API 只做转发：
  ```c
  extern "C" int64_t JS_ToInteger(JSValue *self) {
      return self->toInteger();
  }
  ```

### 9. 运行时一切信息都从 ctx 出发

- 运行时（Runtime）中**一切信息都从 ctx 出发**：`JSContext` 是整个运行时的根/入口。
- 所有 API 都以 `ctx` 作为第一个参数（呼应 #6/#8 的函数签名）。
- 全局状态、作用域、当前执行阶段、堆/内存、错误状态等，都从 ctx 获取或注入。
- ctx 是贯穿所有接口的"上下文根"，**不通过全局变量/单例访问运行时状态**。

---

## 二、值与错误模型

### 4. JSValue 为一等公民，错误通过 kind=EXCEPTION 传递

- **JSValue 是项目的一等公民**：大部分 API / 内部接口都以 **JSValue 作为返回值**。
- 当发生**编译错误 / 执行错误**时，通过 `kind = EXCEPTION` 的 JSValue 向调用方提供错误。
- 调用方拿到返回值后检查 `kind`：若为 `EXCEPTION` 则按错误路径处理。
- 错误载体 = 返回值（kind=EXCEPTION 的 JSValue），**不依赖 C++ 异常**。

### 3. 异常处理：禁止 C++ 异常跨 C ABI 边界

- **本项目代码禁止使用 C++ 的 `throw` 抛出异常**（最终出口是 C，异常不能越过 C ABI 边界）。
- 对标准库可能产生异常的场景（如 `std::vector`/`std::unordered_map` 扩容抛 `std::bad_alloc`、`std::stoi` 抛 `std::invalid_argument`/`std::out_of_range` 等），**必须 `try-catch` 处理**，禁止漏出。
- **错误处理机制**：引擎内部走返回值/错误码（类似 QuickJS 的 `JS_Throw` + 调用方检查），不依赖 C++ 异常。

### 7. 异步/生成器的中断：INTERRUPT_YIELD / INTERRUPT_AWAIT

- **异步（async/await）和生成器（generator）** 的中断运行，也是一种**特殊的 JSValue**。
- 特殊 kind：
  - `INTERRUPT_YIELD` —— 生成器 `yield` 断点
  - `INTERRUPT_AWAIT` —— async 函数 `await` 断点
- VM 执行到 `yield` / `await` 时，不是正常返回结果，而是返回 kind=INTERRUPT_YIELD / INTERRUPT_AWAIT 的特殊值，由宿主/调用方决定何时 resume 继续执行。
- **中断也是一种值**：统一纳入 JSValue 的 kind 体系，不引入额外 C++ 控制流机制。

---

## 三、内存管理

### 5. 内存管理策略

- **外部 API 通过提供分配函数和释放函数来管理内存**（引擎对外暴露 alloc/free 回调，宿主可自定义内存分配器）。
- **内部重写基类的 `new`/`delete`**（统一走可自定义的分配器，例如通过分配器基类或重写 operator new/delete）。
- **引擎内部内存使用堆内存管理，而非栈内存**（引擎对象生命周期由堆管理）。
- **JSValue / JSContext 永远是不透明指针**：对外不暴露内部结构，只能通过访问方法操作。

---

## 四、宿主扩展与 C 函数

### 6. 宿主注入 C 函数（外部接口扩展）

- 外部宿主可**注入 C 函数**向 JS 提供接口（宿主用 C 实现函数，暴露给 JS 调用，类似 QuickJS 的 `JS_NewCFunction`）。
- **C 函数签名格式**：
  ```c
  JSValue *JSCFunction(JSContext *ctx, JSValue *self, size_t argc, JSValue *argv);
  ```
  - `ctx`：JS 上下文
  - `self`：`this` 绑定
  - `argc`：参数个数
  - `argv`：参数数组
  - 返回 `JSValue *`（结果值指针）
- **内部抛异常**：返回 `kind = EXCEPTION` 的 JSValue（与 #4 统一错误机制）。

### 8. 生成器/async 实现机制：switch-case 状态机 + ctx 注入阶段

- 注入的生成器和 async 函数，内部采用 **switch-case 状态机**实现中断和继续执行。
- **阶段（stage）和作用域由引擎通过 ctx 注入**：用 `JS_GetGeneratorStage(ctx)` 读取当前执行阶段，而非函数自己维护状态。
- 一般形式：
  ```c
  JSValue *JSGenerator(JSContext *ctx, JSValue *self, size_t argc, JSValue *argv[]) {
      size_t stage = JS_GetGeneratorStage(ctx);
      switch (stage) {
          case 1:
              // ...
              return JS_CreateInterruptYield(ctx, JS_CreateNumber(ctx));  // yield 断点
          case 2:
              // ...
      }
      return JS_CreateNumber(234);  // 结束返回
  }
  ```
- 关键 API：
  - `JS_GetGeneratorStage(ctx)`：从 ctx 读取当前阶段。
  - `JS_CreateInterruptYield(ctx, value)`：产生 yield 断点（配合 #7 的 INTERRUPT_YIELD）。
- 每次 resume 时引擎重新进入该 C 函数，通过 `switch(stage)` 跳到对应 case 继续执行。
- 本质：生成器/async 的 C 函数 = **由 ctx 驱动阶段的 switch-case 状态机**；断点 = 特殊 JSValue（INTERRUPT_YIELD/AWAIT）；阶段 = ctx 注入。

---

## 五、VM 与并发模型

### 10. VM 采用栈风格（无值寄存器）

- **VM 采用栈风格（stack-based VM）**。
- **没有值寄存器**：不存在 register-based VM 那样的通用值寄存器堆。
- **只有控制寄存器**：如 `PC`（程序计数器）这类控制流寄存器。
- 操作数都压在**操作数栈**上：指令从栈顶取操作数，结果压回栈顶。
- 指令编码简单（无寄存器号），执行步数比寄存器风格多——性能要求不高，可接受。
- 与架构文档 §5.4 一致：栈式 VM（QuickJS / 早期 SpiderMonkey 路线），此处固化为硬规范。

### 11. 单线程模型，裁剪并发特性

- **当前采用单线程**：引擎一次只在一个线程上运行。
- **`Atomics`、`SharedArrayBuffer` 等并发相关特性不需要实现**（裁剪）。
- 内部状态无并发竞争，不需要线程安全锁/原子操作。
- 架构文档 §8 的 B3 批次中 `Atomics`/`SharedArrayBuffer` **从实现范围移除**（已同步更新架构文档）。
- 未来若引入多线程再补充，当前明确不实现。

---

## 六、调试能力（延后实现，但必须有）

### 12. 完整调试能力：DAP 协议 + debugger 关键字

- 引擎需提供**完整的调试能力**，**遵守 DAP 协议**（Debug Adapter Protocol）。
- 支持代码中的 `debugger` 关键字。
- **延后实现**：不属于当前里程碑，但**必须有**（架构上预留）。
- **做成开关可控制屏蔽**：
  - 开关关闭（屏蔽调试）时，在 **AST 阶段直接丢弃 `debugger` 语句**。
  - 即 `debugger` 语句的处理是"编译期可裁剪"的：关闭调试时 AST 不产出该语句，无运行时残留开销。
- 影响架构：
  - 预留调试器/DAP 通道（断点、单步、栈帧、变量等）。
  - `debugger` 语句作为 AST 节点类型存在，由编译开关决定是否生成对应字节码。


| 规范 | 对架构文档的影响 |
|------|------------------|
| #1 嵌入引擎 + C ABI | 新增 C API 层设计（§2.1 分层中"宿主 CLI"扩展为"宿主嵌入 API + CLI"） |
| #3 禁止 C++ 异常 | §5.4 异常模型：确认不依赖 C++ 异常，统一 JSValue 错误值 |
| #4 JSValue 一等公民 | §5 运行时：JSValue 为接口返回值主类型，kind=EXCEPTION 表示错误 |
| #5 内存管理 | §6 GC：外部 alloc/free 回调 + 内部重写 new/delete，堆内存管理 |
| #6 宿主注入 C 函数 | §2.1 / §5.3：内置函数注册为 C 函数，签名统一 |
| #7/#8 生成器中断 | §5.1 指令集/§5.4：yield/await 用 INTERRUPT_YIELD/AWAIT 值 + switch-case 状态机 |
| #9 一切从 ctx 出发 | 全部 API 首参为 ctx，运行时状态从 ctx 获取 |
| #10 栈式 VM | §5.4 确认：无值寄存器，仅 PC 等控制寄存器 |
| #11 单线程 | §8 B3 批次移除 Atomics/SharedArrayBuffer（已更新） |
| #12 调试能力（DAP + debugger） | 预留 DAP 调试通道；`debugger` 语句为 AST 节点，编译开关关闭时在 AST 阶段直接丢弃 |

---

*确认后本规范即为 M0 及后续开发的硬约束。如需调整任何一条，请指出条目编号。*
