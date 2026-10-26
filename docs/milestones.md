# neo.js 里程碑细化（Milestones）

> 版本：v0.2
> 日期：2026-10-21
> 状态：**已确认，M3 之后各阶段细化并标注进展**
> 关联文档：`docs/architecture.md`（架构，§10 里程碑总览）、`docs/coding-standards.md`（编码规范）
> 说明：本文档把架构 §10 的宏观里程碑细化到**可独立验收的子阶段**。
> 总原则：**每次提交都处于可构建、可测试状态**；一个阶段不完成，不进入下一阶段。

---

## 零、总体路线

```
M0 骨架 ─▶ M1 前端流水线 ─▶ M2 解释器核心 ─▶ M3 对象模型 ─▶ M4 内置对象 B0/B1
        ─▶ M5 ES6 完整 ─▶ M6 完整化
```

- **M0 ✅ 已完成**：工程结构、JSValue、诊断系统、CLI 空壳、JSAllocator、Scope-Value 体系。
- **M1 ✅ 已完成**：前端流水线（拆分为 M1.1–M1.6，见 §一）。
- **M2 ✅ 已完成**：解释器核心（拆分为 M2.1–M2.5，见 §二）。
- **M3 ✅ 已完成**：对象模型（拆分为 M3.0–M3.4，见 §三）。
- **M4 ✅ 已完成**：内置对象 B0/B1（拆分为 M4.1–M4.4，见 §四）。
- **M5 🔄 进行中**：ES6 完整（拆分为 M5.1–M5.8，各含字母子阶段，见 §五）。
- **M6 ⏳ 未启动**：完整化（子阶段规划见 §六）。

---

## 一、M1 前端流水线（✅ 已完成，M1.1–M1.6）

> 目标：完整覆盖 ES2023+ 语法（含严格模式、废弃语法警告），以 **AST dump 稳定 + Test262 语法用例冒烟** 为验收。

### M1.1 前端流水线骨架

**内容**：先搭通整个前端管线的空骨架，再逐步填充。

- 目录与模块声明：`include/neojs/frontend/{lexer.h, parser.h, ast.h}` + `src/frontend/`
- `Token{kind, start, end, line, col}` 与 `TokenKind` 枚举的**初步**定义（先覆盖最小子集，随阶段扩充）
- `ASTNode` 基类 + `NodeType` 枚举 + 子节点容器 + 源码位置（先定义框架与少量节点）
- CLI 接入：`--dump-tokens` / `--dump-ast` 两个跟踪开关（架构 §1.3 可观测）
- Diagnostics 通道接入（架构 §7：`DiagnosticSink`），Parser/Lexer 可发错误与警告
- `JS_Eval` 从"M0 未实现占位"升级为：`源码 → Lexer → Parser → (当前阶段) → 输出`，验证错误通道

**核心设计：Lexer 与 Parser 协同（on-demand 拉取），而非先分词再解析**：
- Parser 按需向 Lexer 拉取 token（`advance()` / `peek()`），Lexer 不独立产完整个 token 流。
- Lexer 携带**上下文（当前是读值还是读操作）**：由 Parser 在解析表达式时显式传入，
  供 `/` 等歧义符号判定（除法 vs 正则）。
- 注释是**多字符符号**，词法层贪婪匹配消解：先读 `//`/`/*` 命中即注释，否则回退为单 `/`。

**验收标准**：
- `neojs --dump-tokens '1 + 2'` 输出最小 token 流
- `neojs --dump-ast '1 + 2'` 输出最小 AST（数字 + 二元运算节点）
- 语法错误能带精确位置经 Diagnostics 输出
- **协同模式骨架可工作**：Parser 拉取 token，Lexer 能按上下文（读值/读操作）区分 `/`

---

### M1.2 分词器（Lexer）

**内容**：完整 token 化，不依赖 Parser（可独立测试）。

- **标识符与关键字**：Unicode ID_Start/ID_Continue（经 ICU 或白名单 + 记录差距）、`\uXXXX`/`\u{...}` 转义、上下文关键字识别（`async`/`yield`/`let`/`static`/`get`/`set` 等先作为标识符，由 Parser 上下文判定）
- **字面量**：
  - 数字：十进制/二进制 `0b`/八进制 `0o`/十六进制 `0x`/科学计数/数字分隔符 `_`/BigInt 后缀 `n`
  - 字符串：单双引号、转义序列（`\n` 等、`\x`、`\u`）、**废弃八进制转义 `\1`–`\7` 打警告**
  - 模板字符串：反引号、`${` 进入表达式模式（**需 Parser 协同的模式切换**，见 M1.3）
- **运算符与标点**：全部运算符（含 `??`/`?.`/`**`/`**=`/`??=`/`&&=`/`||=`/`=>`/`...`/`#`）、标点
- **注释**：`//` 行注释、`/* */` 块注释、HTML 风格注释（`<!--`/`-->`，Annex B）
- **废弃语法识别**：旧式八进制字面量 `0123`（非严格）打警告标记（架构 §4.1）
- **Token 位置信息**：start/end/line/col 全量填充

**验收标准**：
- 分词测试套件覆盖全部 token 类别
- 数字/字符串/模板串的各种合法与非法写法均有用例
- `--dump-tokens` 对覆盖样例输出稳定

---

### M1.3 歧义处理（协同模式下的专门处理）

**内容**：JS 词法/语法的歧义点专门处理，独立成一个阶段。

> 前提：Lexer 与 Parser 协同（M1.1 定下的骨架）。所有歧义点都在**这个模型**下消解，
> 不是"先分词再解析"的后处理。

| 歧义点 | 说明 | 处理（协同模型下） |
|--------|------|------|
| **`/` 三义：除法 / 正则 / 注释** | `a / b`（除法）、`/re/g`（正则）、`//`（注释）、`/*`（块注释） | **注释**：多字符符号，词法层贪婪匹配（先读 `//`/`/*` 命中即注释，否则回退为单 `/`），不需上下文。**除法 vs 正则**：Lexer 依据 Parser 传入的**上下文（读值/读操作）**——读值位置 `/` 进入正则扫描模式，读操作位置 `/` 是除法（`re-scan` 规则，§12.6） |
| **模板串 `${}` 嵌套** | `` `a${b}c` ``、模板串可无限嵌套、内部可含反引号 | Lexer 与 Parser 通过**模式栈**协同：遇到 `${` 切到表达式模式，`}` 回到模板模式 |
| **`{` 四义** | 语句块 / 对象字面量 / 解构模式 / 函数体 | Parser 按产生式上下文判定（`{` 后是否 `}` 或 `:` 等） |
| **`(` 语句起始** | 表达式语句不能以 `(` 开头的限制（§12.2） | Parser 处理表达式语句的受限产生式 |
| **数字 vs 属性访问** | `1.toString` 的点号是小数点还是成员访问 | Lexer 数字规则：数字后直接 `.` 且后随数字为小数点，否则 token 给 Parser 判成员访问 |
| **移位 vs 比较** | `>>`/`>>>` 移位 vs `a > > b`（空格分隔） | Lexer 贪婪匹配 `>>`/`>>>`；Parser 不回溯 |
| **上下文关键字** | `async x => …`、`yield`、`let`、`static`、`get`/`set` | Lexer 作标识符，Parser 按上下文识别为关键字语义 |
| **ASI 自动分号插入** | `return\nx`、`a\n++b`、`break`/`continue`/`throw` 行终止限制 | Lexer 记录换行；Parser 按 §12.9 规则询问并插入分号 |
| **箭头函数参数歧义** | `(a, b) => …` vs `(a, b)` 表达式 | Parser 前瞻：`=>` 决定是箭头函数参数 |

**验收标准**：
- **`/` 歧义专项测试集**：`a / b`、`a / /re/`、`/re/g`、`// 注释`、`/* */`、`a = b / c / d` 全部正确——验证**注释贪婪消解**（无上下文）与**除法/正则上下文区分**（有上下文）
- **ASI 专项测试集**：`return`/`break`/`continue`/`throw` 换行、`++`/`--` 后缀、受限产生式
- 模板串嵌套、数字属性访问、上下文关键字用例通过

---

### M1.4 表达式体系（Pratt 解析）

**内容**：从小到大实现完整表达式树。

1. **字面量与标识符**：数字/字符串/正则/`null`/`true`/`false`/`this`/`super`/标识符引用
2. **一元运算**：`!` `~` `+` `-` `typeof` `void` `delete` `++` `--`（前后缀）
3. **二元运算**：算术/比较/相等/位/逻辑/`in`/`instanceof`/`**`/`??`/`&&`/`||`（优先级爬升）
4. **成员访问与调用**：`.`、`[]`、`()`、`?.` 可选链、`new`、`new.target`、`import()` 动态导入
5. **三元与赋值**：`?:`、`=`/`+=` 等复合赋值、`??=`/`&&=`/`||=`
6. **箭头函数**：参数列表 + `=>` + 函数体（表达式体/语句体）
7. **函数/类表达式**：`function`/`async function`/`class`（含 `#x` 私有字段、static 块）
8. **解构**：对象/数组解构模式（含 rest `...`、默认值、嵌套）
9. **模板串与正则**：作为表达式节点（配合 M1.3 歧义处理）

**验收标准**：
- Pratt 优先级正确（`1 + 2 * 3`、`a || b && c`、`??` 与 `||` 混用报错等）
- 赋值方向正确（右结合）、`**` 右结合
- 可选链、解构、箭头函数、函数/类表达式的 AST dump 覆盖 ES2023+ 语法

---

### M1.5 语句体系

**内容**：语句级语法，分小组实现。

1. **声明**：`var`/`let`/`const`（含解构声明）、`function` 声明、`class` 声明、`import`/`export`（模块，可后置到 M1.6）
2. **表达式语句与空语句**：表达式语句（受限产生式）、`;`
3. **块与控制流**：`{}`、`if`/`else`、`while`、`do-while`、`for`、`for-in`、`for-of`、`for await`
4. **跳转**：`return`、`break`、`continue`（含标签 `label:`）
5. **异常**：`throw`、`try`/`catch`/`finally`（catch 参数）
6. **`with` 语句**：识别并**发废弃警告**（非严格模式）；严格模式报 SyntaxError（架构 §4.3/§7）
7. **`debugger` 语句**：AST 节点存在，编译开关关闭时丢弃（规范 #12）

**验收标准**：
- 各语句 AST dump 正确
- 废弃语法警告（`with` 等）经 Diagnostics 正确发出且不中止解析
- 严格模式 `with` 报 SyntaxError

---

### M1.6 兼容收尾

**内容**：语法覆盖面完整性与兼容。

- 严格模式全量（`"use strict"` 指令序言、`class`/`module` 自动严格、严格模式语法限制）
- 模块语法补全（`import`/`export` 全形式、顶层 `await`、`import.meta`、`import()`）
- 剩余 ES2023+ 语法补漏（`#x in obj`、逻辑赋值、`??`/`?.` 边界、正则字面量 `d`/`v` flag 等）
- 废弃语法警告清单全量核对（架构 §7.1 表格）
- Test262 `test/language/` 语法相关子集冒烟

**验收标准**：
- 语法解析冒烟通过（Test262 language 核心子集）
- AST dump 对覆盖样例稳定
- 废弃警告清单逐项可触发

---

## 二、M2 解释器核心（✅ 已完成，M2.1–M2.5 + 总验收）

> 目标：字节码 + 最小 VM，跑通基础脚本（循环、递归、闭包计数器），并把源码求值端到端接入。
> 分层：**字节码发射 API（BytecodeBuilder）由编译器与测试共用**——测试可直接用 `push_op` 构造字节码做端到端运行（对应 M2.1 的发射式接口，Compiler 在 M2.4 复用）。

### M2.1 字节码格式 + BytecodeBuilder 发射 API（✅ 已完成，fdb94aa）

**内容**：

- 字节码格式：`Instruction{op, arg}` 定长 4 字节编码；`FunctionBytecode{code, constants, numLocals}`
- `BytecodeBuilder`：`push_op(op, arg, label)` 发射式 API + 便捷方法（`push_const`/`push_int`/`load_var`/`store_var`/`call`/`jump`/`ret`/`print`）
- 常量池按值去重（`add_constant`）；label 回填（前向跳转记 refs 待 `bind_label` 回填，**已绑定 label 的后向跳转立即回填**）
- 首期指令集：常量/加载/算术/比较/跳转/调用/控制（PushConst…Nop，24 条）

**验收标准**：

- 测试可直接用 builder 构造字节码端到端运行
- label 回填正确（前向/后向跳转目标无误）

### M2.2 栈式 VM 解释器（✅ 已完成，fdb94aa）

**内容**：

- 主循环 `for (;;) { Opcode op = *pc++; switch (op) { ... } }`（架构 §5.4），栈式 VM
- 操作数栈与局部槽同一 vector（栈底 numLocals 个局部槽，之上表达式栈）；入口参数写入局部槽
- **GC 根集：VM 操作数栈入根集**（`ctx->vmStackRoot(&stack)`），解释期间栈上值不可回收；指令执行期间操作数保持栈上、运算完成后才收缩
- 函数调用复用 E3 `call()` 统一入口；算术/比较复用 `operators.h` 语义（`add`/`binaryArith`/`compareOp`/`toNumeric`/`toBoolean`）

**验收标准**：

- 表达式、赋值、if/while/for（跳转）、宿主函数调用跑通
- GC 下栈上值存活（栈帧根集生效）

### M2.3 C API 执行入口（✅ 已完成，fdb94aa）

**内容**：

- `JSBCOp` 枚举（与内部 `Opcode` 数值一致）+ `JS_RunBytecode` 扁平字节码执行入口（`neojs.h`/`capi.cc`）

**验收标准**：

- C 层可直接构造字节码端到端运行（add/循环/字符串拼接/入口参数用例通过）

### M2.4 编译器：AST → 字节码（✅ 已完成，d2dd3b7 / 9cdd586 / ef9663a / 2e2c656）

**内容**：

- 表达式编译：字面量/标识符（局部槽、全局）/算术/比较/逻辑短路（`&&`/`||`/`??`）/成员访问
- 语句编译：表达式语句/赋值/`if`/`while`/`for`/`return`/`{}` 块
- 函数编译：参数入局部槽、`Call`/`Ret`、递归调用
- 顶层程序编译为函数字节码；常量池管理

**验收标准**：

- `JS_Eval` 端到端求值基础脚本（源码 → AST → 字节码 → VM 执行）
- 循环、递归脚本跑通

### M2.5 闭包与作用域链（✅ 已完成，e6322a1）

**内容**：

- 函数闭包捕获词法环境（scope 链入闭包对象）
- 多帧调用栈（Frame 入 GC 根集；栈深度上限防爆栈，架构 §5.4 约 1e6）
- 闭包计数器脚本

**验收标准**：

- 闭包计数器脚本跑通（M2 总验收）
- 递归（含尾递归）脚本跑通

### M2 总验收（✅ 已完成，17c9b4e）

- 跑通基础脚本：**循环、递归、闭包计数器**
- 每次提交可构建、可测试（`cmake --build` 无警告 + 测试全绿）

---

## 三、M3 对象模型（✅ 已完成，M3.0–M3.4）

> 目标：对象/数组字面量、成员读写、原型链、类型转换。对应架构 §10 M3。
> 前置：E1–E5（对象模型地基/类型转换/函数最小集/语法绑定集，`c5c0a62`–`fca7862`）已在 M2 前完成，M3 在此之上补齐编译与运行链路。

### M3.0 前置：对象/数组字面量 AST 节点 + Parser 读值位置 + VM 对象指令（✅ 已完成，aed7d38）

**内容**：

- `ast.h` 新增 `ObjectLiteral`/`ArrayLiteral` 节点（含 `Property`/`SpreadElement` 载荷）
- Parser 区分**读值位置**（`{`/`[` 进入对象/数组字面量而非块/下标），配合 M1.3 上下文
- VM 新增对象/数组构造指令（`NewObject`/`NewArray` + 成员装载）

**验收标准**：

- `({a: 1})`、`[1, 2]` 的 AST dump 与字节码执行正确
- 读值位置歧义（语句块 vs 对象字面量）正确消解

### M3.1 对象/数组字面量编译与执行 + 成员读取（✅ 已完成，d91d45b）

**内容**：

- 编译器：`ObjectLiteral`/`ArrayLiteral` → 构造 + 逐成员装载指令
- 成员读取编译：`obj.prop` / `obj[expr]` → `LoadProp`/`LoadElem`
- 嵌套字面量、字符串/数字键、数组元素

**验收标准**：

- 嵌套对象/数组字面量求值正确
- 成员读取（点号/方括号）端到端跑通

### M3.2 成员赋值编译（✅ 已完成，0ec1694）

**内容**：

- 赋值编译：`obj.prop = rhs` / `obj[expr] = rhs` → `StoreProp`/`StoreElem`
- 复合赋值 `+=`/`-=`/`*=` 等（读-算-写）
- 左值校验（不可赋值的表达式报错）

**验收标准**：

- 成员赋值/复合赋值脚本端到端跑通
- 非法左值编译期报错

### M3.3 原型链与类型转换完善（✅ 已完成，32bc86d）

**内容**：

- `Object` 构造器 + `Object.getPrototypeOf`/`setPrototypeOf`/`create`
- 原型链属性查找（普通/特殊属性顺序）、`__proto__` 语义
- 类型转换链路完善（`toObject`/`toString`/`toNumber` 等与属性访问协同）
- GC 两段式创建 pin/unpin 分配保护

**验收标准**：

- `Object.create(proto)` 原型链脚本跑通
- 装箱/原始值属性访问正确

### M3.4 收尾与验收（✅ 已完成，2d852dd）

**内容**：

- 架构文档 §5.2 `__proto__` 标注 + gc.md 分配保护说明
- 全量构建/测试验收，提交

**验收标准**：

- 对象操作脚本全部通过；文档与实现一致

---

## 四、M4 内置对象 B0/B1（✅ 已完成，M4.1–M4.4）

> 目标：架构 §8 批次 B0（Object/Function/Boolean/Number/String/Symbol/BigInt/Error/globalThis）+ B1（Array/Map/Set）核心内置。对应架构 §10 M4。

### M4.1 Array 构造器与 Array.prototype（✅ 已完成，576705e）

**内容**：

- `Array()`/`Array(len)`/`Array(...items)` 构造器 + `Array.isArray`
- `Array.prototype`：push/pop/shift/unshift/indexOf/join/toString/reverse
- 数组字面量与 `Array()` 原型链统一指向 `Array.prototype`（惰性缓存 + GC 根集）
- `length` 属性读写经 VM `LoadProp`/`StoreProp` 特判（elements 推导 / 截断扩展 + RangeError）
- 新指令 `Dup` + `CallMethod`：成员调用 `obj.method()` 的 this 绑定

**验收标准**：

- 数组构造/方法/长度语义端到端测试全绿

### M4.2 Function/Error 家族（✅ 已完成，9ca9a66）

**内容**：

- `Construct` 抽象运算（§7.3.13）：`new F(args)` 语义
- `Function` 构造器 + `Function.prototype`（惰性创建 + context 缓存 + GC 根集）
- `Function.prototype.call/apply/bind`（bind 重构为 `FunctionKind::Bound`，GC 引用边）
- `Error` 家族：Error/TypeError/RangeError/ReferenceError/SyntaxError + `Error.prototype.toString` + 子类原型链

**验收标准**：

- new 构造/原型绑定/call/apply/bind/Error 家族端到端测试全绿

### M4.3 剩余 B0 基础类型：BigInt + String/Number/BigInt.prototype（✅ 已完成）

**M4.3a–f BigInt 全链路 + String.prototype 补齐（✅ 已完成，1066b50）**

- BigInt 值表示（`Kind::BigInt` + JSValue 大整数存储 + C API kind）
- BigInt 字面量全链路（lexer 字符串 → AST → `PushBigInt` 指令 → VM）
- 转换抽象运算（ToBigInt/ToNumeric/ToString/比较的 BigInt 分支）
- 运算符语义（BigInt 算术/位运算/比较/一元）
- BigInt 构造器 + `BigInt.prototype` + 注册与测试
- `String.prototype` 约 20 方法（at/charAt/codePointAt/endsWith/includes/indexOf/padStart/padEnd/repeat/replace/slice/split/startsWith/substring/toLowerCase/toUpperCase/trim/trimStart/trimEnd/concat 等）
- 新增 utf16 工具模块（UTF-16 长度/索引/大小写/空白/子串）
- new String/Number/Boolean/BigInt 包装对象语义修复（constructWrapperKind）
- String exotic object length/索引属性推导

**M4.3g Number.prototype 补齐 + Number 静态方法（✅ 已完成，b4be9e5）**

- `Number.prototype.toFixed/toExponential/toPrecision/toLocaleString`
- Number 静态方法：isFinite/isInteger/isNaN/isSafeInteger/parseFloat/parseInt
- Number 常量：EPSILON/MAX_SAFE_INTEGER/MAX_VALUE/MIN_SAFE_INTEGER/MIN_VALUE/NaN/NEGATIVE_INFINITY/POSITIVE_INFINITY
- `toString(radix)` 支持 2–36 进制 + conversion 格式化工具

**M4.3h String/Number 方法测试 + 全量验证（✅ 已完成，并入 1066b50/b4be9e5）**

- 端到端测试覆盖 + ASan/valgrind 泄漏验证

### M4.4 B1 集合与 Object 补齐（✅ 已完成）

**M4.4a–e Map/Set 对象模型 + 构造器 + Symbol 键 + 迭代器协议（✅ 已完成，fbba3a7）**

- Map/Set 对象模型与存储（`createMap`/`createSet`，GC 引用边）
- Map 构造器 + Map.prototype（get/set/has/delete/size/keys/values/entries/clear/forEach 等）
- Set 构造器 + Set.prototype（add/has/delete/size/clear/forEach/values/entries 等）
- Symbol 属性键支持（`getOwnSymbolProperty`/`hasOwnSymbolProperty` 等）
- 迭代器协议抽象运算（iterator.h/cc：getIterator/iteratorNext/iteratorStep/iteratorValue/getMethod）

**M4.4f for-of 编译执行 + 内建迭代器（✅ 已完成，fbba3a7）**

- 编译器：`ForOfStatement` 编译（GetIterator/IteratorStep/JumpIfTrue），迭代器存唯一名内部槽
- VM：新增 `GetIterator`/`IteratorStep`/`JumpIfTrue` 指令
- 内建迭代器：Array/String/Map/Set 的 `Symbol.iterator` + `next()`
- 测试：EvalForOf（数组/空数组/嵌套/字符串含增补平面/Set/Map/自定义可迭代对象 + 手工 next 推进）

**M4.4g Object 静态方法补齐（✅ 已完成，26eb687）**

- keys/values/entries：own enumerable string-keyed 键收集，String exotic 虚拟索引
- assign/defineProperty/defineProperties/fromEntries
- freeze/seal/preventExtensions + isFrozen/isSealed/isExtensible（`[[Extensible]]` 标志）
- JSObject 新增 `property_order_` 插入顺序跟踪 + 修复既有泄漏（Compiler 作用域帧池）

**M4.4h Object.prototype 方法补齐 + 访问器属性语义（✅ 已完成，407b340）**

- Object.prototype 6 方法：hasOwnProperty/isPrototypeOf/propertyIsEnumerable/valueOf/toString（含 toStringTag）/toLocaleString
- defineProperty 访问器支持：get/set 解析、可调用校验、读写分发
- VM `[[Get]]`/`[[Set]]`：访问器调 getter/setter，writable=false 拒绝赋值
- 数组 length writable 校验 + Array.prototype 原型链修正（→ Object.prototype）
- Object.values/entries/assign 访问器取值走 getter；freeze/isFrozen 访问器属性正确处理

**验收标准（M4 整体）**：

- 全部 341 个端到端测试全绿；ASan/valgrind 零泄漏

---

## 五、M5 ES6 完整（🔄 进行中，M5.1–M5.8 + 字母子阶段）

> 目标：class/module/generator/async/Promise/Proxy 等 + 模块系统。对应架构 §10 M5、§8 批次 B2（控制/异步）/B3（工具/反射，Intl 可后置）。
> 现状基础：M1.6 ③ 已具备模块**语法层**（Module 根节点 + import/export 全形式 + import.meta + import() 解析）；M4 已具备 new/构造、Symbol、迭代器协议、call/apply/bind、Error 家族。
> 现状缺口（启动前需补）：async/generator/yield **语法与编译**未实现；模块**运行时**（加载器）未实现；可选链赋值等编译未实现（当前报 "not implemented"）。
> 拆分原则：模块、异步、生成器、类均为独立大子系统，各拆为字母子阶段（a→…），一阶段一验收；M5 整体是最大的阶段，不要求一次完成。

### M5.1 语法与编译地基补全（🔄 进行中）

**已落地（M5.1a–M5.1e-g）**：

- M5.1a 解构/默认值/rest/展开编译
- M5.1b 逻辑赋值与运算符补全
- M5.1c 对象字面量 getter/setter/method + 计算键
- M5.1d class 声明/表达式 + super（a–d 子阶段，含 `#x` 私有字段、static 块、类字段、派生类默认构造器参数转发）
- M5.1e-a 运算符补全：in/instanceof/?? + 一元 +~/delete/await + 模板串编译
- M5.1e-b 更新表达式 ++/-- 编译（标识符/成员/私有字段）
- M5.1e-c for-in 编译 + label/break/continue 标签跳转
- M5.1e-d 解构/循环 left 补齐（对象 rest、解构计算键、for-in/of 解构 left、class 计算字段键；修复 `{key = default}` 双 free 崩溃与默认值非 undefined 值丢失）
- M5.1e-e 可选链编译（`?.`/`?.[`/`?.()` 短路；`delete a?.b` 短路返回 true；拦截 `a?.b = 1`/`a?.b++`/`new a?.b()` 语法错误）
- M5.1e-f throw/try/catch/finally 编译 + VM 异常传播（§14.15/§14.16）：`Throw`/`RethrowPending` 指令、`TryEntry` 异常保护表、`ConversionResult.thrown` 携带 JS throw 值、`tryDispatch` 最内层区间匹配、嵌套 try/catch 内再抛/finally 异常覆盖/`catch{}` 无参数；同时完成 VM 主循环重构为全局静态分发表 `kHandlers`（90 opcode 各独立 handler，消除 Debug 模式巨型 switch-case 爆栈）
- M5.1e-g StoreGlobal 与全局赋值/更新补全（§8.1.1.2.2 PutValue）：新增 `StoreGlobal` 指令（追加末尾保持 opcode 数值稳定），未声明标识符赋值（非严格）写入 globalThis 属性；覆盖简单/链式赋值、复合赋值（`+= -= *= /= %= **= <<= >>=`）、逻辑赋值（`??= ||= &&=`）、前缀/后缀 `++`/`--`、函数内全局赋值、解构赋值、for-in/of 全局 left；`EvalStoreGlobal` 26 用例验证读写一致性
- M5.1e-h spread 展开调用补全（§13.3.6/§15.7）：新增 `CallMethodArray` 指令（`[obj, func, args_array]` -> 调用结果，this=obj，追加末尾保持 opcode 数值稳定），接通 4 处此前 not implemented——成员展开调用 `obj.m(...a)`（含计算键，此前 CallArray this=undefined 缺陷修复）、私有方法展开 `obj.#m(...a)`（class 内 this 保留）、`super.m(...a)`（this=当前帧 this）、`super(...a)` 展开构造（复用 SuperCallArray）、`new obj.#c(...a)` 私有构造器展开（LoadPrivate + ConstructArray）；`EvalSpreadCall` 8 用例验证 this 绑定与展开求值

**剩余**：

- 补编译器未实现项（当前 `not implemented` 清单逐项消解，M5.1e）

**验收标准**：

- class/默认参数/rest/展开/解构/逻辑赋值端到端脚本通过
- `not implemented` 编译错误清单清零

**内容**：

- `class` 声明/表达式（含 `#x` 私有字段、static 块、`super`）——语法与编译
- 默认参数/rest 参数编译、展开运算符（对象/数组/调用）编译、解构声明/赋值编译
- 逻辑赋值 `&&=`/`||=`/`??=`、可选链赋值、对象字面量 getter/setter/method
- 补编译器未实现项（当前 `not implemented` 清单逐项消解）

**验收标准**：

- class/默认参数/rest/展开/解构/逻辑赋值端到端脚本通过
- `not implemented` 编译错误清单清零

---

### M5.2 模块系统（✅ 完成：M5.2a–M5.2d）

> 模块语法层（M1.6 ③）已就绪，本节补**运行时执行**语义。模块加载/链接/循环依赖/动态导入各自独立可验收。

**M5.2a 模块加载器与执行（✅ 完成）**

- 模块加载器：JS_AddModuleLoader 宿主源码供给回调 + JS_EvalModule 入口
- 模块记录（JSModuleRecord）：specifier + 模块 env + 导入/导出绑定表 + 状态机
- import/export 运行时绑定：LoadImport 动态查源模块 env（选项 A，live binding）
- 模块求值顺序（依赖先求值）、作用域隔离（顶层 var 不污染全局）
- 默认导出（具名/匿名/表达式 → `__default__` 隐藏绑定）、re-export、`export *`
- 错误路径：import 绑定赋值 TypeError、无 loader、模块不存在、语法错误、顶层 throw

验收：多模块 import/export 脚本端到端运行（tests/test_module.cc 19 用例全绿）

**M5.2b 循环依赖与链接语义（✅ 完成）**

- 模块求值状态机：Uninstantiated → Evaluating → Evaluated（§16.3.1.1），循环依赖回查短路收敛
- 求值顺序：DFS 后序（最深依赖先执行，与规范一致）
- `findExport` 递归解析：本地导出 / 具名 re-export / `export *` 合并，visit 访问集环保护
- 嵌套函数 import 绑定传播：子 builder 继承模块 imports 表（LoadImport 下标一致）
- live binding：函数提升 / `let` 导出在循环依赖中动态读源模块 env

验收：循环依赖（基本/顺序/函数/let live binding）+ `export *`（多源合并/冲突/环）用例全绿（tests/test_module.cc 26 用例）

**M5.2c 动态导入与 import.meta（✅ 完成）**

- `import.meta` 运行时对象（✅）：每模块惰性创建（url = specifier），预置到模块 env 隐藏绑定，LoadImportMeta 沿 env 链定位；闭包内可访问、可扩展、每模块独立
- 动态 `import()` 执行（✅，M5.2c-2）：`import(specifier)` 编译为对全局内置 `import` 的调用（Builtin），运行时 loadAndEvaluateModule（依赖先求值）+ 成功 resolve 模块 namespace 对象 / 失败 reject 错误值；求值同步完成，决议经 Promise 微任务异步投递
- 命名空间导入 `import * as ns`（✅，M5.2d）：新指令 LoadImportNamespace，运行期 createModuleNamespace 生成导出镜像（§10.4.6）；镜像经 findExport 解析 re-export/export * 转发链读 live binding

验收：import.meta + 动态 import（基本/默认导出/依赖链/reject/链式 then/单例）+ 命名空间导入（基本/re-export+export * 合并/静态动态一致性）用例全绿（tests/test_module.cc 41 用例）

**M5.2d 模块系统总验收（✅ 完成）**

- 修复 `findExportRec` re-export 递归：用 ExportEntry.[[LocalName]]（`p as q` 时源模块内查 `p`，§16.4.7）而非外部导出名，支持 as 改名与链式转发
- 修复导出表重复登记：compileModuleExport 两遍编译去重（第 1 遍登记、第 3 遍仅取声明体，register_exports 开关）

验收：模块端到端用例集全绿（加载/链接/循环/动态导入/命名空间，tests/test_module.cc 41 用例，全量 538 用例绿）

---

### M5.3 异步：Promise（✅ 完成）

> Promise 是异步地基，独立成节：先核心机制，再静态/组合方法，最后总验收。

**M5.3a Promise 核心（✅ 完成）**

- `Promise` 构造器 + 状态机（pending/fulfilled/rejected）+ 决议程序（resolve/reject）（✅）：object.h ObjectKind::Promise + promise_state_/promise_result_ 内部槽；promise.cc 构造器/executor、resolve/reject 决议（§27.2.1.4/§27.2.1.2）、thenable 接合（§27.2.2.3 经微任务）、恒等/抛错传播
- `Promise.prototype.then/catch/finally` + 微任务队列（job queue）（✅）：reaction 存 `__reactions__` 属性（fulfill/reject handler 双存），settle 统一派发；job queue 由 JSContext 持有，宿主经 `JS_RunJobs` 驱动；then 链式返回/多 then 顺序/微任务在同步代码后执行
- 共享内部函数缓存（✅）：PromiseInternal 枚举 8 项（Resolve/Reject/ReactionRunner/ThenableRunner/Identity/Throw/FinallyThen/FinallyCatch）惰性创建 + 进根集；Bound 闭包承载捕获数据（决议闭包/reaction job/thenable job/finally 包装）

验收：Promise 链/then 回调端到端脚本通过（✅ tests/test_promise.cc 17 用例全绿，全量 512 绿）

**M5.3b Promise 静态与组合方法（✅ 完成）**

- `Promise.resolve/reject`（✅）：resolve 遇 Promise 原样返回，否则 new Promise(res => res(x))；reject 直接 reject
- `Promise.all/allSettled/any/race`（✅）：可迭代对象遍历（getIterator/iteratorStep/iteratorValue）；可变共享状态存状态对象属性（`__remaining__`/`__result__`/`__errors__`/`__settled__`），元素回调 = Bound 闭包（state+index），index 高位标记区分 fulfill/reject；all 全收后 resolve 结果数组、allSettled 收记录数组（{status,value/reason}）、any 任一 fulfill 短路 / 全 reject 抛 AggregateError、race 先到先得（settled 短路）
- AggregateError 近似实现（✅）：用 Error 构造器 + name/errors 属性（message 传 undefined 避免数组 ToPrimitive 失败）；完整构造器留 M5.6+
- 共享内部函数缓存扩展（✅）：PromiseInternal 枚举扩至 12 项（+AllElement/AllSettledElement/AnyElement/RaceElement），context.h 缓存数组与索引检查同步扩至 12

验收：组合方法用例（含空数组/拒绝分支）通过（✅ tests/test_promise.cc 25 用例全绿，全量 520 绿）

**M5.3c Promise 总验收（✅ 完成）**

- 补充验收用例（✅）：all 延迟 settle 保序收集、finally 对 reject 透传原 reason、then 返回 rejected promise 接合、then 非函数参数恒等、allSettled 延迟 settle 保序、嵌套组合（all 内嵌 race/any）、thenable 异步 resolve 接合

验收：Promise 用例集全绿（链/组合/异常传播/微任务顺序）（✅ tests/test_promise.cc 32 用例全绿，全量 527 绿）

---

### M5.4 异步：async/await（✅ 完成，M5.4a–M5.4c）

> 依赖 M5.3 Promise 与 M5.1 语法地基；async 函数编译为状态机。

**M5.4a async 函数编译（✅ 完成）**

- `async function` / `async` 箭头函数 / async 方法语法与编译（await 表达式）
- await 状态机保存/恢复：AsyncFrame（GC 对象）持 fn/pc/栈/env/this/pending，
  挂起时 VM 现场从 C++ 栈转移到堆，AwaitResolve/AwaitReject 续延以微任务
  job 形式入队（Bound 闭包捕获 AsyncFrame）；resumeAsyncFrame 重建现场
  从挂起点继续
- 脚本顶层 var 双写全局对象（§13.3.2.4 CreateGlobalVarBinding）：
  顶层函数声明独立编译走 LoadGlobal 读全局对象，普通闭包 LoadUpvalue
  读 top_env，op_StoreVar 汇聚点双写保证两路径一致

验收：async 函数 + await 端到端脚本通过（20 用例）

**M5.4b await 语义深化（✅ 完成）**

- await 对 thenable 的接合（PromiseResolveThenableJob 复用 M5.3 机制）
- await thenable reject 路径修复：runBytecode 新增 initial_thrown 参数，
  resumeAsyncFrame 的 is_reject 分支传 rejection 值作为启动异常，从挂起点
  后第一条指令经 tryDispatch 派发（命中 try/catch 恢复，否则向上传播
  reject 外层 promise）
- `for await...of` 依赖 M5.5 异步迭代器，本阶段延后

验收：thenable await fulfill/reject/异步 thenable 用例通过

**M5.4c async 总验收（✅ 完成）**

验收：async/await 用例集 27 用例全绿（含异常/嵌套/并发/Promise.all/
async 方法 this/多次挂起恢复现场完整性）；全量 566 测试全绿

---

### M5.5 生成器与迭代器深化（⏳ 未启动，M5.5a–M5.5d）

> 依赖 M4.4 迭代器协议与 M5.1 语法地基；生成器是独立状态机子系统。

**M5.5a generator 编译与运行时（⏳ 未启动）**

- `function*` / `yield` / `yield*` 语法与编译
- generator 对象/状态机（next/throw/return 恢复）

验收：generator 脚本（含 yield* 委托）通过

**M5.5b 迭代器协议对齐（⏳ 未启动）**

- 内建迭代器与协议抽象运算进一步对齐（架构 §8 B2）
- `Iterator` 相关方法（Symbol.iterator 全量）

验收：迭代器协议用例集通过

**M5.5c AsyncGenerator / for await（⏳ 未启动）**

- `AsyncGenerator` 对象/状态机 + `for await...of` 完整执行

验收：async generator 用例通过

**M5.5d 生成器总验收（⏳ 未启动）**

验收：生成器/迭代器用例集全绿

---

### M5.6 RegExp + Date（⏳ 未启动，M5.6a–M5.6b）

**M5.6a RegExp（⏳ 未启动）**

- `RegExp` 对象：字面量/构造器、exec/test、flag 语义
- String 方法正则分派（Symbol.replace/search/match/split 等）

验收：正则匹配/替换端到端脚本通过

**M5.6b Date（⏳ 未启动）**

- `Date` 对象：构造/解析/格式化方法（toISOString/get/set 等）

验收：Date 用例通过

---

### M5.7 Proxy / Reflect（⏳ 未启动，M5.7a–M5.7b）

**M5.7a Proxy 核心（⏳ 未启动）**

- `Proxy` 构造器 + trap 分派（get/set/has/deleteProperty/apply/construct 等）

验收：Proxy trap 用例（含 revocable）通过

**M5.7b Reflect（⏳ 未启动）**

- `Reflect` 静态方法（与 Object 方法对齐，走 Proxy trap）

验收：Reflect 用例通过

---

### M5.8 M5 总验收（⏳ 未启动）

- Test262 ES6 相关子集高通过率
- 每阶段可构建、可测试

---

## 六、M6 完整化（⏳ 未启动，子阶段规划）

> 目标：剩余标准库、Intl 可选项、WeakRef 等、性能调优。对应架构 §10 M6、§8 批次 B1 剩余（ArrayBuffer/TypedArray/DataView/WeakMap/WeakSet）/B3 剩余（Math/JSON/WeakRef/FinalizationRegistry）。

### M6.1 剩余标准库（⏳ 未启动）

**内容**：

- `Math`/`JSON` 静态对象
- `ArrayBuffer`/`TypedArray`（10 种）/`DataView`
- `WeakMap`/`WeakSet`/`WeakRef`/`FinalizationRegistry`（GC 关联语义）

**验收标准**：

- 各标准库对象方法端到端测试通过

### M6.2 Intl（可选项，⏳ 未启动）

**内容**：

- `Intl` 依赖 ICU（已接入）或自研，作为独立可选项（架构 §11）

**验收标准**：

- Intl 核心对象（NumberFormat/DateTimeFormat/Collator）用例通过（或明确裁剪记录差距）

### M6.3 性能调优与演进扩展点（⏳ 未启动）

**内容**：

- 演进路线（架构 §11）：Shape/Hidden Class + Inline Cache、基线 JIT（模板解释器）、分代/增量 GC、调试器/Inspector
- VM/GC 基准与调优

**验收标准**：

- 基准脚本性能达标；无回归

### M6.4 Test262 全量达标（⏳ 未启动）

**内容**：

- `test/built-ins/` 与 `test/language/` 核心子集全量跑测
- 剩余差距记录并裁剪

**验收标准**：

- Test262 全量目标达成率

---

## 七、跨阶段的质量红线

- **每阶段可构建、可测试**：提交前 `cmake --build` 无警告 + `ctest` 全绿。
- **测试先行**：每个歧义点/语法类别配套独立测试集（`tests/frontend/`）。
- **AST dump 是主要观测手段**：`--dump-ast` 输出稳定，作为语法正确性的快速回归。
- **废弃语法必须告警**：不能静默通过（架构 §7）。

---

*M5/M6 为规划拆分，启动前按实际需要可再细化；M3/M4 状态标注以 git 提交为准。如需调整子阶段划分、顺序或验收标准，请指出条目编号。*
