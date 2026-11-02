# Neo.js 交接文档（会话切换入口）

> 更新: 2026-11-02 · 用途: 新会话快速接手项目

## 一、项目是什么

从零自研的**可嵌入 ECMAScript 标准运行时**（neo.js），C++20 实现，
无 Node 标准库，只有 JavaScript 标准库。本体作为嵌入库供其它项目使用，
允许外部注册自定义模块。

**核心卖点**：无重依赖 · 可嵌入 · 标准覆盖 · 工程治理

**动机**：V8 等大厂引擎依赖重/构建体系庞大不适合嵌入；QuickJS 等社区项目
标准支持不全/调试功能不全/工程治理弱，二次开发成本高。

## 二、当前状态

**纯设计阶段，零代码**。已固化 27 个 ADR（架构决策）+ 1 份规划大纲。
**尚未开始任何编码，未创建任务清单。**

## 三、核心架构（务必先读 ADR-001）

### 五阶段管线
```
JS 源码 → [Lexer+Parser 合一](AST) → Sema → Compiler → 字节码 → VM 执行
```

### 分层模型
| 层 | 职责 | 状态 |
|----|------|------|
| **Engine** | 语言核心：Value/Scope/Context/Function/对象模型/标准库 | 持久状态 |
| **VM** | 执行中枢：栈模式字节码解释、PC 跳转、指令分发 | 临时状态（一次任务） |

### 核心原则
- 依赖方向：**VM → Engine**（Engine 不依赖 VM）
- VM 栈模式：操作数在栈上，statement 保证栈平衡
- VM 生命周期：每次函数调用/模块执行创建 → return/halt 销毁
- 挂起/恢复：Engine + VM 状态都保存
- native/脚本等价：native 通过 Engine API 硬编码，与脚本语言核心等价
- 异常即值：异常是特殊 Value，走 return 通道，无跨 VM 栈展开

## 四、ADR 索引（docs/decisions/）

| ADR | 主题 | 状态 |
|-----|------|------|
| 001 | 整体架构（五阶段/Sema/Engine-VM分离/Lexer-Parser合一） | ✅ |
| 002 | GC 抽象 | ⏳ 待写 |
| 003 | Unicode 策略（ICU 内嵌方案，D:\projects\icu） | ✅ |
| 004 | 执行模型（VM生命周期/栈模式/挂起恢复） | ✅ |
| 005 | native 等价性 | ✅ |
| 006 | 异常即值 | ✅ |
| 007 | 开发规范与约束（8 条） | ✅ |
| 008 | 内存管理架构（Allocator/GC 分层） | ✅ |
| 009 | 源码结构、API 风格、内部组织 | ✅ |
| 010 | 接口-实现-工厂两层设计 | ✅ |
| 011 | Engine 组织架构（Context 门面） | ✅ |
| 012 | Scope 树与 GC（scope_ref 根+图遍历） | ✅ |
| 013 | 闭包与作用域模型（三层作用域） | ✅ |
| 014 | 变量提升/TDZ/遮蔽 | ✅ |
| 015 | Value 内存模型（meta+data/ref 计数） | ✅ |
| 016 | Type 体系（meta 类，统一分派器） | ✅ |
| 017 | 类型清单 + Interrupt/Exception | ✅ |
| 018 | 对象模型（三容器+原型指针） | ✅ |
| 019 | 赋值语义（两层共享模型） | ✅ |
| 020 | 标准库组织（全局变量注册） | ✅ |
| 021 | Function 结构（tag union + self） | ✅ |
| 022 | 函数调用（invoke 统一入口+字节码序言） | ✅ |
| 023 | 协程挂起/恢复（yield/await 参数桥接） | ✅ |
| 024 | 字节码指令集与栈契约（三段式/call N/序言/箭头bind_this/const时序） | ✅ |
| 025 | Sema 两遍 walk 与闭包穿透捕获（shadow_scope/穿透登记/eval回退） | ✅ |
| 026 | M0 项目搭建与基础设施（CMake+Ninja+Clang/GTest submodule/ICU port） | ✅ |
| 027 | 测试规范与覆盖率门禁（分层/llvm-cov/严格指标/M2起强制） | ✅ |

## 五、关键设计速览

### 核心语言模型
```
Context（门面/根）→ Scope（作用域）→ Value（值）
Context 持有：global（树根）/ root（模块根）/ current（当前）三 scope
Environment 仅作访问器，搁置（非核心）
```

### 内存与 GC
```
Value = meta（Type，语言 API）+ data（数据，ref 计数）
GC 管 Value 销毁（scope_ref!=0 根 + 图遍历），引用计数管 data 销毁
Allocator（外部注入）与 GC（Engine 创建）分层，生命周期归 Context
两层共享：第一层 scope→value（仅 import/闭包），第二层 value→data（复杂对象/字符串）
```

### 闭包与作用域
```
函数持 closure_scope（持久，parent=定义词法作用域 root_scope）
调用时：closure_scope（持久）→ argument_scope（临时，实参）→ body_scope（临时，局部）
函数孤立化：查找只需 closure → root_scope → global
```

### 函数调用
```
Value* Function::invoke(Context* ctx, Value* self, size_t argc, Value* argv[]);
Function 保存 rootScope（模块作用域，invoke 时切换）
参数逆序压栈，self 在栈顶
字节码序言：define 形参槽 → StoreThis → Store a/b → RestToArray → Store c → push_scope
```

## 六、开发约束（ADR-007）

1. **C++ 异常**：编译不禁用（标准库需要），自有代码禁止 throw/try-catch，标准库边界兜底
2. **测试框架**：git submodule 引入 GTest
3. **编译告警**：-Werror
4. **指针/哨兵**：禁止 nullptr 当哨兵（用业务语义如 undefined）；内部 C++ API 参数可信；C API 判空 panic
5. **内存管理**：Allocator 与 GC 分层（ADR-008）
6. **编码风格**：组合优于继承，依赖注入，先设计后编码
7. **编码/格式化**：LF + UTF-8 无 BOM，clang-format
8. **正确性纪律**：Windows + WSL 双平台编译运行，Valgrind + ASan

## 七、开发纪律（用户指定）

- **设计先行 + 测试先行**，逻辑拆分足够细，先出验证用例再开发
- 小步提交且可验证
- **先完整设计再动代码**（重要：不要急着创建任务/写代码）
- 文档沉淀用 README + docs/（design/ + decisions/ + spec/）

## 八、外部资源

- **ECMAScript 规范**：`D:\ECMA-262_17th_edition_june_2026.pdf` / `.txt`（第 17 版，2026-06）
- **ICU 内嵌方案**：`D:\projects\icu`（CMake FetchContent + 数据内嵌，零外部运行依赖）
- **参考项目**：`D:\projects\clux`（负责人之前的编译器/VM 工程，C11，含 sema 模块，架构可借鉴）

## 九、下一步待办

**M0 项目搭建已完成**（ADR-026/027）：目录骨架 + CMake(Clang+Ninja) + 配置文件 +
GTest submodule + ICU port 占位 + HelloWorld + 双平台构建/测试/ASan 验证通过。
测试分层（unit/integration/regression）+ 覆盖率工具链（llvm-cov source-based）已就绪，
M2 起强制覆盖率门禁（整体行90/分支80/函数90，核心模块上浮）。
**首次 commit 已完成**：`21984ae M0: 项目搭建与基础设施 + 设计文档落盘`。

**里程碑采用滚动式规划**（ADR-未编号/plan.md §四）：完成一个再定下一个，
不一次性制定全部里程碑。粗粒度规划会失真——如"五阶段跑通"实则词法/语法
分析各自是巨大工作量，不应塞进一个 M。

设计阶段的**架构层 + 开发规范层**已收敛。待继续：
1. **M1 规划**：M0 收尾后，根据当前状态决定 M1 范围（候选焦点：
   Allocator 接口落地、Value 体系形态、GC 抽象接口等；自底向上，前层被后层依赖）
2. **其余业务模型**：native 模块作用域（ADR-022 遗留）、对象/数组字面量、
   try/catch（异常即值走 return 通道）、for 循环字节码（ADR-024 待定项）
   ——随相关里程碑推进时细化，不预先全定

## 十、记忆系统

`memory/` 目录是持久记忆（跨会话），含：
- `project_architecture.md`：核心架构决策演进（最完整，新会话必读）
- `user_profile.md`：用户画像（资深编译器/VM 工程师，偏好设计先行）
- `reference_resources.md`：外部资源位置

新会话应**先读 memory/ 与本文档**，再深入具体 ADR。
