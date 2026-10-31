# neo.js 交接文件（会话切换）

> 生成时间：2026-10-31
> 分支：`master` · 工作区：**有未提交改动（M5-B 函数体系 + 运行时选项体系，待验收提交）** · 全量测试：**234/234 全绿**
> 最近提交：`649e80d`（M5-A 语句体系：编译器语句编译 + vm 控制流/TDZ/var 提升）
> 待提交改动：sema 独立阶段 + 函数定义/调用（M5-B）+ 运行时选项体系，详见第六节

---

## 一、项目是什么

neo.js：从零实现的 JS 引擎（C++、手写、**字节码 VM**、ES2023+、**正确性优先**）。

核心原则：**除 AST 外完全重写**。当前处于**第二阶段重写**（M0–M5-A 已完成）：
旧解释器（M1–M5.5f，含 generator/async/Promise/class 等，历史提交在 git 里）已整体废弃，
现在按「core 数据层 → GC → engine 运算 → vm 字节码执行」的新架构重来。

引擎总架构（用户亲自拍板，务必遵守）：
- **字节码前** = 纯编译器（源码 → AST → 字节码），**无任何运行时语义**
- **字节码后** = 执行器，分 **vm + engine** 两层
  - **engine**：基于 `Context` 的 value 运算模块（模块不是类），提供几乎完整的语言特性，
    与前端/vm 完全解耦；操作全部以 `Context&` 为首参，返回 `core::Value*`
  - **vm**：解析字节码，逐条调用 engine 接口完成语义；**操作数栈由 vm 持有**（借用 value 指针，不计数）
- **scope 对 vm 不可见**：scope 树归 Context，vm 不直接操作 scope
- 异常用**返回 exception value**（`ValueKind::Exception`），**不抛 C++ 异常**

参考项目：`D:/projects/clux`（clux，用户指定参考）。

---

## 二、当前代码结构（重写后）

```
include/neojs/
  core/   allocator.h context.h data.h gc.h scope.h value.h
  engine/ engine.h
  vm/     bytecode.h compiler.h vm.h
  frontend/ ast.h diagnostics.h frontend.h lexer.h parser.h   ← 复用旧前端（AST 除外原则）
  host/   （空）
  neojs.h
src/
  core/   allocator.cc context.cc gc.cc
  engine/ engine.cc
  vm/     bytecode.cc compiler.cc vm.cc
  frontend/ …（旧前端，复用）
  host/   （空）
  main.cc ← CLI 调试工具（M5-B 实现）：--dump-ast / --dump-bytecode / --run（win32 wmain 转 UTF-8）
src/vm/  ← M4/M5-A 新增
  bytecode.h/.cc：Opcode 枚举 + Instruction{op,arg} + ConstEntry + Bytecode（常量池去重）+ FunctionInfo{offset,end} 函数偏移模型
  compiler.h/.cc：compile_expression(ASTNode*, Bytecode&) 表达式编译器 + compile_program/compile_statement 语句编译（M5-A）
  vm.h/.cc：Vm 栈式解释器 + 函数指针分发表；handler 返回 value 模型，pc 归 vm 持有
src/engine/  ← M3 新增
  engine.h/.cc：create/转换/一元/二元/比较/异常 全套接口
  ⚠ 无短路逻辑（logical_and/or/nullish 已删，短路归编译器跳转）
tests/
  test_main.cc test_core.cc test_gc.cc test_engine.cc test_frontend.cc test_vm.cc test_stmt.cc
  （旧 test_m55d.cc 等已不在——重写后清理，只剩上述 7 个）
CMakeLists.txt
  NEOJS_CORE_SOURCES glob: src/core/*.cc + src/frontend/*.cc + src/engine/*.cc + src/vm/*.cc
```

---

## 三、已完成的里程碑（重写后，git 提交）

| 阶段 | 提交 | 内容 |
|---|---|---|
| M0 | `8717925` | 变量作用域重做：除 AST 外完全重写，清空旧 runtime/compiler/CAPI |
| M1 | `0d936f9` | core 数据层：value/data 分层 + scope 树 + scope_ref 引用计数 |
| M2 | `a7046ea` `baabd36` | GC：合并协程现场（YieldEnv/AwaitEnv→Coroutine）；Gc 全局表唯一 owner + 延迟回收 + collect 扫描 |
| M3 | `09c96b4` | engine 层：基于 Context 的 value 运算模块（与 vm/编译器解耦） |
| M4 | `a83fbab` `3197c56` | vm 层：表达式字节码生成器 + 栈式 VM 解释器 + 端到端测试；后重构为函数指针分发表 |
| M5-A | `649e80d` | 语句体系：编译器语句编译（声明/控制流/return）+ vm 控制流/TDZ/var 提升；块级 TDZ 哨兵预声明 + var 提升 DefineVarGlobal |

M1 前还有一次 M1.1（`0d936f9` 之前）的 core 数据层提交，`git log` 可见。

---

## 四、当前能力（M4 表达式 + M5-A 语句，已交付）

流水线已打通：**源码 → AST → 字节码 → VM 执行**。

M4 表达式：
- 字面量（number/string/boolean/null/undefined）
- 变量加载（`LoadVar`，含全局内置 undefined/NaN/Infinity 特判）
- 一元（`+ - ! ~ typeof void`）
- 二元（算术/位/比较/`== ===`）
- 逻辑短路 `&&`/`||`/`??`（编译器生成跳转，`JumpIfFalse/True/NotNullish`）
- 条件表达式 `cond ? a : b`（`Conditional`）

M5-A 语句：
- 变量声明 var/let/const（**完整块作用域 + TDZ**）
- 表达式语句（含标识符赋值 `x = rhs` → StoreVar 保留栈顶）
- 控制流 if/else、while/do-while/for（消费版跳转 `JumpIfTruePop/JumpIfFalsePop` 保证栈平衡）
- return（`Ret` → interrupt 解包返回）
- **块级 TDZ**：进入块先为直接 let/const 发 `PushUninitialized+DefineVar` 哨兵，初始化时 StoreVar
- **var 提升**：`DefineVarGlobal` → `ctx.define_var`（M5-A=全局，M5-B 扩展为函数级）
- 函数偏移模型：`Bytecode.functions` + `FunctionInfo{offset,end}`（函数体编译/调用 M5-B 落地）

### 关键实现细节（下次会话必读）

1. **指令集**（bytecode.h）：`Opcode` 枚举末尾有 **`Count` 哨兵**，`vm.cc` 分发表用它定大小 + `static_assert` 校验全覆盖。**加新指令必须：枚举加项 + 写 handler + 表里填一行**。
2. **短路语义归编译器**（用户明确纠正）：engine 侧**没有** `logical_and/or/nullish`。编译器 `compile_short_circuit` 布局：
   ```
   compile lhs; jump_if_xx END; pop(丢掉 lhs); compile rhs; END:
   ```
   - `&&`=JumpIfFalse、`||`=JumpIfTrue、`??`=**JumpIfNotNullish**（新增，方向与 nullish 相反）
   - 非跳转分支必须 `pop` 掉 lhs，否则栈残留污染后续指令
3. **条件表达式**（compiler.cc Conditional）：`compile cond; jump_if_false ELSE; compile a; jump END; ELSE: pop(丢掉 cond); compile b; END:`——注意 conditional 的跳转要 pop 掉 cond（区别于短路保留栈顶）。
4. **handler 返回 value 模型**（vm.cc，用户重构）：handler 返回 `Value*`——`nullptr`=无产出（jump/pop）、正常 value=压栈、Exception=透传、Interrupt=解包返回。**pc 归 vm 持有**（协程挂起打包整个 vm，pc 随 vm 保存，§9）。每个 handler **自己推进 pc_**（next()/jump_to()，读完自己的操作数后 ++pc_）。分发表 `static inline constexpr Handler kHandlers[]` 归 Vm 私有。
5. **全局内置常量**：`undefined`/`NaN`/`Infinity` 是 Identifier 非 Literal，`LoadVar` 里特判（不依赖 scope）。
6. **操作数栈**：vm 持 `vector<Value*>`，**借用指针不计数**；临时 value 由 engine create 挂 ctx current_scope（scope_ref++），随 scope 退出回收。
7. **语句控制流栈平衡**：if/while/do-while/for 用**消费版跳转** `JumpIfTruePop/JumpIfFalsePop`（判定后 pop 栈顶），两条路径栈均干净；表达式短路用保留版 `JumpIfTrue/JumpIfFalse`。
8. **TDZ 哨兵预声明**（compiler.cc）：块/Program 进入时 `collect_tdz_names` 收集直接 let/const → PushScope 后逐个 `PushUninitialized+DefineVar`；VariableDeclaration 若名字已预声明则只发 init+StoreVar（否则如 for init 先发哨兵+DefineVar）。`LoadVar` 遇 Uninitialized → ReferenceError。
9. **函数 AST 布局**：FunctionDeclaration/Expression = `[name?, params..., body]`（fn_has_name 标记 children[0]）；CallExpression = `[callee, args...]`；ArrowFunctionExpression = `[params..., body]`。前端 parser 已完整支持（parser_stmt.cc）。

---

## 五、测试基线

- 全量：**234/234 通过**（Windows `ctest` 与 WSL `./neojs_test` 双平台一致）
- Vm 专项：15/15；Stmt 专项：**30/30**（M5-A 18 + M5-B 函数 7 + 选项体系 7，其中 2 个选项相关归入 M5-A 段，详见第六节）；Allocator 专项：2/2（分配上限测试）
- sema 专项：8/8（tests/test_sema.cc，shadow scope 树 + 捕获列表）
- 构建：`cd /d/projects/neo.js/build && cmake --build . --target neojs_test`
- WSL 调试构建：`cd /d/projects/neo.js && rm -rf build-wsl && mkdir build-wsl && cd build-wsl && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug .. && cmake --build . -j 8`（ICU 自动探测 `third_party/icu/download/` 离线包，无需联网；build-wsl 已被 .gitignore 的 `build*/` 覆盖）
- ⚠ 顶层 `return` 是 SyntaxError（前端 parser 正确拒绝），M5-A 无函数无法测 return 传播，等 M5-B 函数体内补
- 调试用 CLI：`./neojs --dump-bytecode --eval 'var x=5; if(x>3)x=1;else x=2;'` 或 `--run`（见第八节）
- 环境：Windows 11 + WSL Ubuntu-24.04（g++/gdb/cmake/ninja），Windows 用 clang++，ICU 用 FetchContent 离线包，gtest 用 submodule

---

## 六、当前进度（M5-B 函数：已完成，待验收提交）

M5-B 函数体系已完整落地（sema → 函数值 → 编译 → vm 执行 → 端到端测试），双平台 227/227 全绿。

**已落地内容**：
1. **sema 独立阶段**（src/frontend/sema.cc，AST 后 compile 前，§10.8）：shadow scope 树 + 捕获列表；
   var 提升到函数级、let/const 到当前块；标识符解析挂 `node->resolved`，跨函数引用加入 `node->captures`。
2. **函数值模型**：CreateFunction（按 FunctionInfo{offset,end,param_count} 建函数值）、SetClosure（捕获挂载）、
   Call（调用序：closure scope → argument scope → body scope，§7；每调用新 vm 从 offset 跑到 Ret）。
3. **两阶段编译**（compiler.cc）：阶段 1 主流程 CreateFunction + 捕获 LoadVar/SetClosure；阶段 2 函数体追加到
   尾部回填 offset，主流程末尾 Halt 隔离。
4. **调用语义**：实参不足→undefined、多余→忽略；无显式 return→undefined；return interrupt 传播；
   闭包捕获共享同一 value（读写穿透）；递归、工厂函数均通过。
5. **CLI 调试工具**（main.cc）：`--dump-ast / --dump-bytecode / --run / --eval <src> / <file>`（win32 wmain 转 UTF-8）。
6. **Allocator 分配上限**：atomic 计数活跃分配字节，超限 oomAbort（防死循环 OOM 耗尽系统资源）。

**M5-B 关键调试结论（已沉淀 memory，下次遇到直接查）**：
1. **Opcode 枚举与 handler 分发表必须顺序一一对应**：bytecode.h 在枚举中间插 CreateFunction/SetClosure/Call，
   vm.h kHandlers 却追加末尾 → 后续指令整体错位（表现：not a function / vector 越界 / OOM 死循环）。
   `static_assert` 只查表大小查不出，指令集扩展时 handler 表必须**同位置插入**。
2. **for-init 作用域**：`for(let i...)` 若 compiler 不 PushScope+compile_scope_entry，let 绑定从不创建 → StoreVar 失败。
3. **Allocator::reserve 无符号下溢**：`size > limit - cur` 在 cur>limit 时下溢成巨大数不 abort；
   改为 `cur > lim || size > lim - cur`（WSL 非 MSVC 分支暴露）。
4. **sema 捕获误判形参**：函数体内引用本函数形参被误判为跨函数捕获（body scope 是函数级且在 arg_scope 之上）；
   修复口径：先找当前函数节点 cur_fn，跨过 `node != cur_fn` 的函数级 scope 才算真捕获；
   且 found_scope 本身是外层函数体时也要计为跨函数（break 移到 is_function 检查之后）。

**端到端测试**（tests/test_stmt.cc，Stmt.Func* 7 个）：
FuncDeclCall / FuncNoExplicitReturn / FuncParamCountMismatch / FuncClosureCapture / FuncClosureFactory /
FuncRecursion / FuncMutateOuterVar。

---

## 六.5 运行时选项体系（directive 序言，M5-B 附加，已完成）

完整打通 **directive → AST → 字节码 → Scope 选项 → 运行时语义**。用户拍板方案：**选项放 Scope 沿 parent 链查找**（生命周期=作用域生命周期，异常/跳转路径天然安全），**删 DisableOption**，顶层选项先挂 root_scope（后续模块体系迁到模块 globalScope）。

**已落地内容**：
1. **parser 收集 directive**（parser_stmt.cc/parser.cc）：Program/Module 顶层、函数体（parseBlock detect_strict）收
   集连续字符串表达式指令到 `ASTNode::directives`（Program/Block/函数节点）。
2. **Scope 选项存储**（scope.h）：`options_` map + `get_option(name)` 沿 parent 链查找 + `set_option`/`has_option_local`。
3. **Context 代理**（context.h/.cc）：get_option/set_option/has_option 代理到 current_scope；新增 `define_global`（隐式全局挂 root_scope）。
4. **字节码穿透**（compiler.cc）：作用域入口 `compile_option_entry` 发 `EnableOption`（scope 销毁自动恢复，**无对称 DisableOption**）；
   vm `h_enable_option` 设到 current_scope。
5. **第一个选项消费方**（vm.cc h_store_var）：非严格模式赋值未声明标识符 → `ctx.define_global`（隐式全局）；
   严格模式 → ReferenceError。严格判定查 "use strict" 沿链查找（函数级/脚本级严格都能命中）。
6. **函数级严格**：函数体 `"use strict"` 进 body scope，函数返回 scope 销毁自动恢复（不污染调用方）。

**选项体系关键调试结论（已沉淀 memory，下次遇到直接查）**：
1. **CLI 是独立 target**：`--dump-bytecode` 看不到 EnableOption 时先 `cmake --build . --target neojs neojs_test`（只
   构建 neojs_test 不会重建 CLI，会用旧字节码）。
2. **选项生命周期=作用域生命周期**：DisableOption 已删，任何「正常结束对称关闭」断言都不再成立；顶层选项挂
   root_scope 常驻（脚本级严格语义，随 ctx 销毁才消失），测试断言要按 scope 方案写。

**选项测试**（tests/test_stmt.cc，7 个）：
OptionUseStrictEnabled / OptionDisabledAfterProgram / OptionCustomDirective / ImplicitGlobalNonStrict /
ImplicitGlobalBlockedByStrict / FunctionLevelUseStrict / FunctionLevelUseStrictRestored。

**下一步候选（M5-C，未拍板，需与用户对齐）**：
- 函数进阶：默认参数（AssignmentPattern）、rest 参数、箭头函数表达式体已支持？、
- 对象/数组字面量 + 成员访问（MemberExpression 目前只走 sema 递归，编译器未落地成员读写）
- this / 方法调用 / new（NewExpression 指令已建模型）
- 异常体系：try/catch/throw + Error 对象（ThrowStatement 已有 AST）

---

## 七、用户协作偏好（务必遵守）

- **中文交流**，技术术语保留英文
- **先对齐规范/方案再动手**：架构分叉点先用 AskUserQuestion 对齐，不要擅自决定
- **打断立即停手**：用户说停就停
- **正确性优先**，性能留待后续
- 参考 **clux**（D:/projects/clux）
- 复用本地资源：ICU 用 FetchContent 离线包、gtest 用 submodule、**不用 vcpkg**、不用 pcre2
- 大功能按里程碑推进，一阶段一验收（构建 + 单测 + 提交）
- 用户认可「质量重构」：如巨型 switch → 函数指针表这类重构，用户会主动要求
- 提交风格：中文 commit message，heredoc 格式，说明改动内容

---

## 八、常用命令

```bash
# 构建测试
cd /d/projects/neo.js/build && cmake --build . --target neojs_test
# 跑全量
cd /d/projects/neo.js/build && ctest
# 跑 vm 专项
cd /d/projects/neo.js/build && ctest -R '^Vm'
# 单个测试
cd /d/projects/neo.js/build && ./neojs_test --gtest_filter=Vm.LogicalShortCircuit
# CLI 调试：dump AST / dump 字节码 / 执行
cd /d/projects/neo.js/build && ./neojs --dump-ast --eval 'var x = 1;'
cd /d/projects/neo.js/build && ./neojs --dump-bytecode --eval 'var x = 1;'
cd /d/projects/neo.js/build && ./neojs --run --eval 'var x = 1;'  # 返回栈顶 value
# 传复杂源码建议写文件再传路径（避免 bash 引号转义）
# WSL 调试（gdb）：
cd /mnt/d/projects/neo.js/build-wsl && gdb --args ./neojs --run file.js
```

---

## 九、当前任务清单（Task 工具）

重写后 M0–M5-A 的任务（#1–#7）已全部 completed。
新阶段（M5-B 函数）开始前先建任务清单，用 TaskCreate 按子阶段拆分。
