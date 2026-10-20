# neo.js 引擎架构设计

> 版本: v0.1（草稿，待评审）
> 日期: 2026-10-17
> 状态: 设计阶段

## 1. 目标与定位

### 1.1 项目目标

- **兼容性优先**：以完整实现 ECMAScript 标准为最高目标，覆盖 ES2023+（含 ES6 全部语法与标准库）。
- **性能要求低**：不追求接近 V8/JSC 的性能，采用**字节码虚拟机**解释执行路线（类似 QuickJS / 早期 SpiderMonkey）。
- **正确性驱动**：以 Test262（ECMAScript 官方一致性测试套件）作为主要验收标准。
- **显式诊断**：对**废弃语法**（如 `with` 语句、`eval`、`arguments.callee`、八进制字面量等）必须能**正确发出警告（deprecation warning）**，而非静默忽略。

### 1.2 非目标（v1 范围外）

- JIT 编译（基线 JIT / TurboFan 式优化 JIT）——留作后续演进方向，架构上预留扩展点。
- Web API（DOM、fetch、console 之外的宿主环境）。
- 跨平台嵌入 API 的完整绑定（仅提供最小 `Realm` 概念）。

### 1.3 设计原则

| 原则 | 说明 |
|------|------|
| 正确性 > 性能 | 任何优化不得改变语义；语义等价是硬约束 |
| 标准可追溯 | 每个特性对应 ECMA-262 的章节号，便于查证 |
| 模块边界清晰 | 词法/语法/语义/运行时四层严格分层 |
| 可观测 | 内置 `--trace` 系列调试选项，可输出 token / AST / bytecode 等 |
| 增量演进 | 以里程碑方式推进，每步可构建、可测试 |

## 2. 总体架构

```
┌──────────────────────────────────────────────────────────┐
│                         Host / CLI                        │
│            (REPL、文件执行、--trace、警告输出)               │
└──────────────────────────────┬───────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────┐
│                     Frontend (前端)                       │
│  ┌──────────┐   ┌───────────┐   ┌───────────┐            │
│  │  Lexer   │──▶│  Parser   │──▶│  Compiler│            │
│  │ (扫描器) │   │ (AST)     │   │ (字节码)   │            │
│  └──────────┘   └───────────┘   └───────────┘            │
│     Token 流        CST/AST          Bytecode             │
└──────────────────────────────┬───────────────────────────┘
                               │
┌──────────────────────────────▼───────────────────────────┐
│                        Runtime (运行时)                   │
│  ┌────────────┐  ┌────────────┐  ┌─────────────────────┐  │
│  │    VM       │  │   Values   │  │     Objects         │  │
│  │ (字节码解释)│  │ (值表示)    │  │ (对象模型/属性系统)  │  │
│  └────────────┘  └────────────┘  └─────────────────────┘  │
│  ┌────────────┐  ┌────────────┐  ┌─────────────────────┐  │
│  │    GC      │  │  Builtins  │  │   Exceptions        │  │
│  │ (垃圾回收)  │  │ (内置对象) │  │ (异常/诊断)          │  │
│  └────────────┘  └────────────┘  └─────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

**数据流**：源码 → Lexer（Token 流）→ Parser（AST）→ Compiler（Bytecode）→ VM 执行。整个管线编译期与运行期分离，编译失败只产生错误/警告，不进入运行期。

### 2.1 分层职责

| 层 | 模块 | 职责 | 对应 ECMA-262 章节 |
|----|------|------|---------------------|
| 词法 | Lexer | 字符流 → Token；处理 Unicode、模板串、正则字面量、自动分号插入（ASI）辅助 | 5, 11, 12 |
| 语法 | Parser | Token 流 → CST/AST；递归下降；`with` 等废弃语法在此识别 | 12–15 |
| 语义 | Compiler | AST → Bytecode；作用域解析（lexical scoping）、变量提升、闭包捕获、`with`/`eval` 作用域模式切换 | 16（代码求值）、10（作用域） |
| 运行时 | VM | 字节码解释执行；指令分发、异常抛出、函数调用/返回 | 8, 9, 10, 17+ |
| 运行时 | Values | JS 值表示、类型转换、数字运算 | 6, 7 |
| 运行时 | Objects | 对象模型、属性访问（get/set/delete）、原型链 | 6.1.7, 10, 19 |
| 运行时 | GC | 内存管理（标记-清除，见 §6） | — |
| 运行时 | Builtins | 标准库内置对象（Object/Array/Function/RegExp/Promise/…） | 19–27 |
| 运行时 | Exceptions | 错误类型、栈回溯、警告通道 | 8.4, 19.5 |
| 宿主 | CLI | REPL、文件执行、跟踪输出、警告显示 | — |

## 3. 值表示（Value Representation）

> 这是引擎正确性与 GC 的基础，先行设计。目标 ES2023+ 需要支持：`undefined, null, boolean, number, bigint, string, symbol, object` 八种类型，以及 `NaN/-0/Infinity` 等特殊值。

### 3.1 方案：NaN-boxing / Pointer-tagging

推荐采用 **指针标记 + 内联小整数（pointer tagging with inline tagged integers）** 的紧凑表示（64 位）：

```
┌───────────┬────────────────────────────────────────────┐
│ tag(16b)  │  payload (48b)                              │
└───────────┴────────────────────────────────────────────┘
```

- `0x0000` … 小整数（int32 内联，带符号扩展）
- `0x0001` … double 装箱指针（HeapValue）
- `0x0002` … string 装箱指针
- `0x0003` … object 装箱指针
- `0xFFFF` … 特殊值（undefined / null / true / false / NaN 用编码位区分）

**理由**：
- 与 C++ 的 `std::variant` / `std::any` 相比，JSValue 只需 8 字节，传参拷贝开销低，Cache 友好。
- 小整数（最常见值）零分配。
- 指针标记天然区分 heap 对象与立即数，便于 GC 遍历与 `===` 快速比较。

**BigInt**：超出 48 位小整数的整数装箱到 heap；`0n`/小 bigint 可内联优化（v1 可先全部装箱，正确性优先）。

**特殊值编码**：`undefined`、`null`、`boolean` 使用特殊 tag + 低 bit 区分，保证：
- `typeof` 快速判断
- `===`/`==` 的 fast path
- `Number` 特殊值（NaN、±Infinity、-0）在 heap 装箱或使用 double 位模式，`-0` 需要显式区分（`1 / -0 === -Infinity`）。

### 3.2 字符串

- 采用 **rope / 扁平化（flatten-on-write）** 策略：`String` 内部为 UTF-16（ECMA-262 按 UTF-16 code unit 定义），提供 8-bit/16-bit 双表示（同 V8 的 sequential string），小字符串短串内联优化。
- 字符串相等性、哈希、`String.prototype` 方法在扁平化后直接索引。
- Interned 字符串（属性名）复用：属性键统一走 `InternTable`（类似 `std::unordered_map`），加快属性查找。

### 3.3 属性键

- 属性名优先使用 interned string；Symbol 属性单独处理（`WellKnownSymbols` 表）。
- 数组索引键（`0, 1, 2, …`）与命名属性分离存储，为数组做 fast path（见 §5.2）。

## 4. 前端（Frontend）

### 4.1 Lexer（扫描器）

- 输入：UTF-8 源码（CLI 层转换为 UTF-16 供 Lexer 消费，或 Lexer 直接处理 UTF-8——v1 采用 Lexer 输入统一 UTF-16，与标准章节一致）。
- 输出：`Token{kind, start, end, line, col}`，字符串/数字/正则/模板串等携带语义值。
- **协同模式（Lexer 与 Parser 一体）**：Parser 按需向 Lexer 拉取 token（`advance()`/`peek()`），
  Lexer 携带**上下文（读值 vs 读操作）**由 Parser 显式传入，供 `/` 等歧义符号判定。
  注释是**多字符符号**，词法层贪婪匹配消解（先读 `//`/`/*` 命中即注释，否则回退为单 `/`），不需上下文。
- 特性：
  - Unicode 转义（`\u{1F600}`）、标识符的 Unicode ID_Start/ID_Continue（v1 可用简单白名单 + ICU，若环境无 ICU 则先 ASCII + 常见 BMP）。
  - 模板字符串（`` ` `` 嵌套、`${}` 表达式）、正则字面量的 lex 模式切换。
  - 数字字面量：十进制/二进制/八进制/十六进制/科学计数/`BigInt` 后缀/数字分隔符 `_`。
  - 自动分号插入（ASI）：按标准 §12.9 规则，Parser 需要时询问 Lexer。
  - **废弃语法识别**：`with` 关键字、旧式八进制字面量（`0123`，非严格模式）、`arguments.callee` 等在此打标记（emit 位置信息），交给诊断系统。

### 4.2 Parser（语法分析）

- **递归下降 + 运算符优先级爬升（Pratt）** 实现表达式；语句用递归下降。
- 产出 **CST/AST**（v1 直接产出 AST 节点，`NodeType` 枚举 + 子节点向量，带源码位置）。
- 特性：
  - 严格模式（`"use strict"` 指令、`class`/`module` 自动严格）、`with` 在严格模式下报错。
  - 作用域跟踪（`Scope` 栈）——供 Compiler 与诊断使用：记录 `let/const/var/function/class/import` 声明、参数、catch 参数。
  - 语法错误（SyntaxError）带精确位置；**废弃语法警告**在此层也可发出（如 `with`）。
  - 支持：class（含私有字段 `#x`、static 块）、模块（import/export、顶层 await）、async/await、generator、`for await`、解构、可选链、空值合并、`??=`/`&&=`/`||=`、BigInt、正则字面量、`import()` 动态导入、`new.target`、`import.meta`、`globalThis` 等 ES2023+ 语法。
  - 记录 **deprecation diagnostics**（`with`、`eval`、`arguments`、八进制字面量、`Function` 构造器字符串等），供 §7 诊断系统消费。

### 4.3 Compiler（字节码生成）

- 输入 AST，输出 **函数级 Bytecode**（每个函数一个 `FunctionBytecode` 对象）。
- 作用域/闭包：
  - 静态词法分析（scope resolution），变量引用在编译期解析为：局部 slot 索引 / 闭包单元（upvalue）索引 / 全局。
  - **变量提升**：`var` 提升 + `let/const` TDZ（时间死区）检查。
  - **`with` 语义**：`with` 内的标识符解析必须动态化（每处标识符查找走运行时 `[[Environment]]` 解析）——编译期将 `with` 作用域内的标识符标记为 `dynamic`，生成对应指令（类似 V8 的 "context" 模式），并**发出废弃警告**。
  - **`eval` 语义**：直接 eval 会引入动态作用域，编译 `eval` 调用处的作用域为 `dynamic`，同样告警。
- 输出指令集见 §5.1。
- 常量池（constant pool）、字符串 intern 表由 Compiler 统一维护。

## 5. 运行时（Runtime）

### 5.1 指令集（Bytecode）

> 原则：**面向对象模型的指令少而语义清晰**，正确性优先，不追求指令密度。参考 QuickJS 指令集风格。

| 类别 | 示例指令 | 说明 |
|------|---------|------|
| 常量/加载 | `push_const`, `push_undefined`, `push_null`, `push_true/false`, `push_int`, `push_double`, `push_string` | 压栈 |
| 变量 | `load_var {slot}`, `store_var {slot}`, `load_upvalue {idx}`, `store_upvalue`, `load_global {name}`, `store_global` | slot=局部变量索引, idx=闭包单元索引 |
| 算术 | `add`, `sub`, `mul`, `div`, `mod`, `pow`, `neg` | 数值运算（含 ToNumeric 转换、`+` 的字符串拼接规则） |
| 比较 | `eq`, `neq`, `strict_eq`, `strict_neq`, `lt`, `gt`, `le`, `ge` | 对应 `==`/`===`/`<`/… |
| 位/逻辑 | `bit_and`, `bit_or`, `bit_xor`, `bit_shl`, `bit_sar`, `bit_shr`, `logical_and`, `logical_or`, `nullish` | `&&`/`||`/`??` 短路用跳转实现 |
| 跳转 | `jump {target}`, `jump_if_true {target}`, `jump_if_false {target}`, `jump_if_nullish` | 控制流 |
| 对象 | `new_object`, `new_array`, `get_prop {key}`, `set_prop {key}`, `delete_prop {key}`, `has_prop` | 属性系统入口 |
| 函数/调用 | `call {argc}`, `call_method {argc}`, `construct {argc}`, `new_function {closure}`, `new_closure {func_index}` | 函数调用/构造 |
| 特殊 | `typeof`, `instanceof`, `in`, `new_target`, `import`, `yield`, `await`, `spread`, `destructure` | 语言特性 |
| 控制 | `ret`, `throw`, `try`, `catch`, `finally`, `pop`, `dup`, `swap`, `check_tdz` | 栈操作/异常处理 |

- **异常处理**：`try/catch/finally` 编译为异常处理表（`exception table`，范围 + handler 地址），VM 抛异常时查表跳转，类似 V8 的 try-catch 表；`finally` 用 `finally block` 子调用或跳转表实现（v1 用跳转表）。
- **调试/跟踪**：每条指令可关联源码位置（line/col），供 `--trace-bytecode` 与未来调试器使用。

### 5.2 对象模型

- **Shape / Hidden Class**：v1 阶段对象属性直接放 `std::unordered_map`（正确性优先）；预留 `Shape` 抽象（§8 演进路线），后续切换不影响语义。
- **数组**：`elements` 分离存储（连续索引）+ `properties`（命名属性），`length` 由元素存储推导；快速路径：`arr[i]` 整数索引 → 直接索引 elements。
- **原型链**：`[[Prototype]]` 内部槽（`JSObject::prototype_`，GC 引用边）；`__proto__` 访问器留待 M4（经 `Object.getPrototypeOf/setPrototypeOf` 读写内部槽）；属性查找遵循普通/特殊属性顺序。
- **访问器属性**：getter/setter（`Object.defineProperty`）与数据属性统一在 `PropertyDescriptor` 中建模。
- **属性访问顺序**（§10.1.3 语义）：ordinary `[[Get]]` 沿原型链；`Proxy` 走 trap（v1 先实现 `Proxy` 基础，`Reflect` 同步）。
- **Well-known symbols**：`Symbol.iterator`、`Symbol.toPrimitive` 等预置表，供内置协议（迭代、toPrimitive）使用。

### 5.3 函数与调用约定

- **函数对象**：`[[Callable]]`、`[[Construct]]`、`[[ThisMode]]`（严格/非严格）、`[[Environment]]`（闭包）、`[[FormalParameters]]`。
- **调用约定**：VM 内部 C++ 函数与 JS 函数统一入口（`JSValue Call(JSValue fn, JSValue thisVal, JSValue* args, int argc)`）；内置函数注册为 C++ 回调。
- **this 绑定**：非严格模式 `this` 装箱（primitive→Object）、`null/undefined` → global；严格模式透传。
- **arguments 对象**：非严格模式映射式 `arguments`（带 deprecation 提示）、严格/箭头函数为独立对象。
- **尾调用**（tail call）v1 不做 TCO（ES2017 起 TCO 为可选实现特性），语义上按普通调用处理。

### 5.4 执行模型

- 字节码解释器主循环：`for (;;) { Opcode op = *pc++; switch (op) { ... } }`，栈式 VM（操作数栈 + 调用栈）。
- 调用栈以帧（Frame）组织：`Frame{ fn, pc, fp(栈底), closure, this }`；栈深度上限（防爆栈，如 1e6）。
- **异常**：C++ 层用 `throw Exception(ErrorValue)` 中断解释，VM 顶层 catch 后查异常表；跨内置 C++ 回调边界用 RAII 守卫确保 GC 一致。

### 5.5 类型转换

- 完整实现抽象运算：`ToPrimitive`、`ToNumber`、`ToString`、`ToBoolean`、`ToObject`、`ToPropertyKey`、`ToNumeric`、`ToBigInt`、`SameValueZero` 等，作为 Values 模块的核心 API（§9 接口清单），所有内置方法复用同一套。

## 6. 内存管理与 GC

- **策略**：**标记-清除（mark-sweep）**，先实现正确性，后引入分代/增量。
- **堆**：统一 `Heap`，对象用 `std::unique_ptr` 或自建 arena 分配；v1 可用 `std::shared_ptr` + 手动根集标记。
- **根集（Roots）**：VM 操作数栈、调用栈帧、全局对象、模块表、常量池、内置对象表。
- **标记**：从根集出发沿对象图遍历（对象 → 属性值、闭包 → upvalue、函数 → 闭包单元）。
- **清除**：回收未标记对象；`WeakMap/WeakSet/WeakRef` 的弱语义在标记阶段特殊处理（v1 先支持 `WeakMap/WeakSet` 的基本语义，`WeakRef`/`FinalizationRegistry` 延后）。
- **与 C++ 集成**：JS 对象持有 C++ 侧 handle（如 `std::shared_ptr<HeapObject>`）避免悬挂；**不采用 V8 式精确两阶段 GC**，降低工程复杂度，接受吞吐略低。
- **内存上限**：可配置，超出抛 `RangeError`。

## 7. 诊断与警告系统（Deprecation / Warnings）

> 这是本项目的一个特色需求：废弃语法不能静默通过。

### 7.1 设计

- **诊断通道（Diagnostics）**：编译期与运行期共用一套诊断输出接口：`DiagnosticSink{ emit(Diagnostic) }`，诊断带 `{severity, code, message, source location}`。
- **严重级别**：`Error`（SyntaxError 等）、`Warning`（deprecation）、`Info`（trace）。
- **废弃特性清单**（v1 覆盖）：
  | 特性 | 触发点 | 标准/来源 |
  |------|--------|-----------|
  | `with` 语句 | Parser 识别 `with` | §14.1；严格模式禁用 |
  | 直接 `eval` | Compiler 检测调用 | §18.2.1；严格模式受限 |
  | `arguments.callee` | 属性访问到 `callee` | §10.2 / Annex B |
  | `arguments.caller` | 属性访问 | Annex B |
  | 八进制字面量（非严格） | Lexer `0123` | §12.9.3 / Annex B |
  | 非严格八进制转义 `\1`–`\7` | Lexer | Annex B |
  | `Function` 构造器（字符串形式） | 运行时调用 | §20.2.1.1 |
  | `escape/unescape` | 运行时调用 | Annex B |
  | `RegExp` 的 `compile` 方法 | 运行时调用 | Annex B |
- **警告输出**：默认输出到 `stderr`，格式 `warning: [DEP-0001] 'with' statement is deprecated... at <file>:<line>:<col>`；CLI 提供 `--no-deprecation-warnings` 与 `--throw-deprecation`（把警告升级为错误）开关。
- **运行时 deprecation 通道**：运行时（如 `arguments.callee`）通过 `Runtime::DeprecationWarning(code, msg, loc)` 输出，与编译期统一格式。

### 7.2 与 Parser 的关系

Parser 在识别废弃语法时**不中止**，只记录诊断；只有严格模式下 `with` 等才报 SyntaxError（按标准）。

## 8. 内置对象（Builtins）范围与优先级

> 对应 ECMA-262 第 19–28 章。按依赖顺序分批次实现（里程碑中细化）。

| 批次 | 对象 | 备注 |
|------|------|------|
| B0（基础） | `Object`, `Function`, `Boolean`, `Number`, `String`, `Symbol`, `BigInt`, `Error` 家族, `globalThis` | 语言核心，先于一切 |
| B1（集合/结构） | `Array`, `ArrayBuffer`, `TypedArray`（10 种）, `DataView`, `Map`, `Set`, `WeakMap`, `WeakSet` | 数据结构 |
| B2（控制/异步） | `Promise`, `Generator`/`Iterator` 协议, `AsyncFunction`, `AsyncGenerator`, `RegExp`, `Date` | 流程与异步 |
| B3（工具/反射） | `Math`, `JSON`, `Reflect`, `Proxy`, `Intl`（可后置）, `WeakRef`, `FinalizationRegistry` | 工具与反射；单线程模型，`Atomics`/`SharedArrayBuffer` 不实现（见规范 #11） |
| B4（模块/宿主） | `import`/`export` 模块加载器, `import.meta` | 模块系统 |

- **`console`**：非标准，作为 CLI 宿主扩展提供（`console.log` 等）。
- 每个内置方法按 ECMA-262 章节逐步实现，配套 Test262 用例。

## 9. 目录结构

```
neo.js/
├── CMakeLists.txt
├── docs/
│   ├── architecture.md          # 本文档
│   ├── bytecode.md              # 指令集详细定义（v0.2 补全）
│   └── milestones.md            # 里程碑与验收标准（v0.2 补全）
├── include/neojs/
│   ├── frontend/
│   │   ├── lexer.h
│   │   ├── parser.h
│   │   └── ast.h
│   ├── compiler/
│   │   └── compiler.h
│   ├── runtime/
│   │   ├── value.h              # JSValue 定义
│   │   ├── object.h             # 对象模型
│   │   ├── string.h
│   │   ├── function.h
│   │   ├── vm.h
│   │   ├── heap.h               # GC
│   │   ├── builtins.h
│   │   ├── error.h
│   │   └── diagnostics.h        # 诊断/警告系统
│   └── host/
│       ├── cli.h
│       └── realm.h
├── src/
│   ├── main.cc                  # CLI 入口
│   ├── frontend/…
│   ├── compiler/…
│   ├── runtime/…
│   └── host/…
├── tests/
│   ├── unit/                    # C++ 单元测试（GoogleTest）
│   ├── conformance/             # Test262 子集跑测脚本
│   └── fixtures/                # 手写 JS 测试用例
└── third_party/                 # 可选：ICU、gtest、test262（submodule）
```

## 10. 里程碑（M0–M6）

| 里程碑 | 内容 | 验收标准 |
|--------|------|----------|
| **M0 骨架** | 工程结构、JSValue、诊断系统、CLI 空壳 | `neojs --version`；JSValue 单元测试 |
| **M1 前端** | Lexer + Parser（ES2023+ 语法）+ 语法错误/废弃警告。**巨大阶段，拆分为 M1.1–M1.6**（流水线骨架 → 分词 → 歧义处理 → 表达式 → 语句体系 → 兼容收尾），详见 `docs/milestones.md` | 各子阶段独立验收，最终 AST dump 稳定 + Test262 语法冒烟 |
| **M2 解释器核心** | 字节码 + 最小 VM（表达式、赋值、if/while/for、函数、闭包） | 跑通基础脚本：循环、递归、闭包计数器 |
| **M3 对象模型** | Object/Array/原型链/属性访问、类型转换 | 对象操作脚本通过 |
| **M4 内置对象 B0/B1** | Object…Set 等核心内置 | 对应 Test262 用例通过 |
| **M5 ES6 完整** | class/module/generator/async/Promise/Proxy 等 + 模块系统 | Test262 ES6 相关子集高通过率 |
| **M6 完整化** | 剩余标准库、Intl 可选项、WeakRef 等、性能调优 | Test262 全量目标达成率 |

> 每个里程碑结束时：构建通过 + 单元测试通过 + 手写/Test262 冒烟用例通过。

## 11. 演进路线（预留扩展点）

- **Shape / Hidden Class + Inline Cache**：对象模型替换为 shape 驱动，不改语义。
- **基线 JIT（模板解释器）**：热点函数编译为机器码，VM 层保留作为 fallback。
- **分代 GC / 增量标记**：在 mark-sweep 基础上演进。
- **调试器 / Inspector 协议**：基于源码位置表与 `--trace-bytecode` 扩展。
- **Intl**：完整 `Intl` 依赖 ICU 或自研，作为独立里程碑。

## 12. 关键技术风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| ES2023+ 特性覆盖面巨大（Intl、正则增强、AsyncIterator、`Array.prototype` 大量方法） | 工期长 | 分批里程碑 + Test262 量化进度 |
| `with`/`eval` 的动态作用域与字节码静态编译冲突 | 语义正确性风险 | 编译期识别并切到 dynamic 模式（§4.3），单独测试集 |
| Unicode 处理（标识符、正则、大小写转换） | 一致性 | 引入 ICU（third_party），或先限 BMP 并记录差距 |
| GC 与 C++ 回调互操作（对象生命周期） | 悬挂/泄漏 | handle 化 + 根集明确 + RAII 守卫 |
| Test262 全量跑测工程量 | 验收困难 | 先跑 `test/built-ins/` 与 `test/language/` 核心子集 |

---

*附：本文档待评审项*

1. 值表示选择（§3）：指针标记 vs NaN-boxing —— 二选一确认。
2. 是否引入 ICU（§12）——影响 Unicode 与 Intl。
3. GC 采用 mark-sweep 是否可接受（vs 引用计数）。
4. 里程碑粒度是否符合预期节奏。
