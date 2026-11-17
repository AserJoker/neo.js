# ADR-033: eval 在孤立函数模型上的实现 — eval_ref 整链保活

- 状态: 已接受 (2026-11-04)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-013（闭包三层作用域 / 函数孤立化）、ADR-012（Scope 树）、
          ADR-022（函数调用）、ADR-025 §6（eval 的 under-capture 回退）、
          ADR-014（提升 / TDZ）

## 背景

ADR-013 §3 确立**函数孤立化**：函数运行时查找链只有
`body → argument → closure_scope → root_scope → global`，**不依赖定义时的中间词法作用域链**
（中间层在定义点之后已销毁）。闭包变量靠 Sema 收集穿透捕获列表 + 运行时
`setClosure(fn, name)` 逐个绑定（共享 Value）来保活。

这与**直接 eval** 的语义冲突：`eval(code)` 可在**调用方作用域运行时注入变量并访问
调用方局部变量**，而 Sema 静态看不见 → 捕获列表不完整（under-capture，ADR-025 §6）。
ADR-025 §6 预留了"回退 Environment 链保护现场"的方向，但未定义具体机制。

本 ADR 定义在孤立函数模型上落地该回退的机制：**eval_ref 整链保活**。

## 决策

### 1. Sema 标记 eval_flag

函数体内出现**直接 eval**（`eval(code)`，非 `var e = eval; e(...)` 间接调用）时，
该函数标记 `eval_flag`。

- 间接 eval 在全局执行，不注入调用方作用域，**不标记**（ADR-025 §6）。
- with 是 over-capture，**不标记**（ADR-025 §6）。

### 2. eval_flag 函数创建时：closure_scope 挂 current_scope + 整链 eval_ref++

普通函数：closure_scope 挂到 **root_scope**（模块隔离，ADR-013）。
eval_flag 函数：closure_scope 挂到**定义点的 current_scope**，并从 current_scope 起
沿 parent 链向上直到长效 scope（模块 root），**每个 scope 的 `eval_ref++`**。

```
普通函数：  closure_scope.parent = root_scope
eval 函数： closure_scope.parent = current_scope（定义点）
            current_scope → ... → root_scope 每个 eval_ref++
```

这样 eval 执行时的查找链自然经过定义点的整条作用域链，可动态访问外层变量。

### 3. popScope：eval_ref > 0 时标记孤儿并延迟销毁

```
popScope():
  if current->eval_ref > 0:
      current->orphan = true      // 标记为孤儿（已 pop 但被 pin 住）
      current = current->parent   // 不销毁，链保持
  else:
      销毁 current；current = current->parent
```

### 4. eval_flag 函数销毁时：自底向上回收

```
销毁 eval_flag 函数:
  1. 先销毁 closure_scope（它是函数对象持有的）
  2. 从 closure_scope->parent 起向上逐层:
       scope->eval_ref--
       if scope->eval_ref == 0 && scope->orphan:
           销毁 scope（孤儿且不再被 pin）
       else if scope->eval_ref == 0 && !scope->orphan:
           不处理（仍活跃在 current 链上，等正常 pop）
```

**回收顺序必须自底向上**：先销毁 closure_scope，再处理 parent 链。若顺序反了，
先回收父 scope 会连带销毁 closure_scope。

### 5. 安全性：eval_ref 的单调性

**单调性**：若子 scope 的 `eval_ref > 0`，则其所有祖先的 `eval_ref > 0`
（因为整链一路 ++ 到长效 scope）。

**逆否**：若某 scope 的 `eval_ref == 0`，则其所有子孙的 `eval_ref == 0`。

由此推出两条安全保证：

- **`~Scope` 递归销毁不误伤受保护 scope**：被销毁的 scope `eval_ref == 0`，其所有
  子孙也必然 `eval_ref == 0`，不存在"父销毁连带销毁受保护子 scope"。
- **parent 链不悬垂**：受保护的 scope 其整条祖先链都被 pin 住，链不会断。

### 6. 作用域链同一性

函数调用**同步切换 root/current**（ADR-022），因此 `current_scope` 与 `root_scope`
必在同一链条上，**不存在跨模块断裂**。eval_flag 函数的 closure_scope 挂 current_scope
后，其 parent 链最终到本模块 root_scope，语义成立。

### 7. 孤儿回收的边界：活跃 vs 孤儿

回收时 `eval_ref` 归 0 的 scope 分两种：

- **孤儿**（`orphan == true`）：之前因 `eval_ref > 0` 被延迟销毁，已 pop。→ 真正回收。
- **活跃**（`orphan == false`）：仍在 current 链上（如外层函数尚未返回）。→ 不处理，
  等其正常 pop 时销毁。

**没有孤儿标记会误销毁活跃 scope**：嵌套 eval_flag 函数场景下，内层函数销毁会把
外层函数的执行作用域 `eval_ref` 减到 0，但该作用域仍活跃，不能销毁。

## 后果

- 优点：在孤立函数模型上支持直接 eval，无需引入完整的 Environment 链；
  eval_ref 单调性保证安全性，实现简单（计数 + 孤儿标记）；
  非 eval 函数零开销（不标记、不计数）。
- 缺点：含 eval 的作用域链被延迟销毁（内存延迟释放到 eval 函数销毁）；
  需在 Scope 增加 `eval_ref` / `orphan` 两个字段；
  eval 函数销毁时需自底向上遍历 parent 链（有开销，仅 eval 路径）。
- 待定：eval_flag 在字节码/FunctionData 中的具体表示（如 FunctionData 增加 bool 字段）、
  `eval_ref` 计数是否需要记录"pin 起点"以支持更复杂的嵌套回收场景，
  后续在实现前细化。

## 实现落地（2026-11-17）

上述机制在运行时以如下形态落地（字段名与 ADR 草案不同）：

- **动态作用域标记**：`Scope::dynamic_` 取代 `eval_flag`；直接 eval 执行前对调用方
  current 调 `markDynamic()`，`dynamicChain()` 沿 parent 链继承。定义点处于 dynamic
  链上的函数由 `createNativeFunction` 自动进入 Environment 模式
  （`linkEnvironmentCapture`：closureScope `linkLookupParent(current)` 并沿链
  `captureRef++`），复用 ADR-036 的 `captureRef` / `autoDispose` 孤儿回收，不再新增
  `eval_ref` 字段。
- **编译期直接 eval 调用点**：Compiler 对非可选的 `eval(...)` 发射 `DirectEval`，
  带展开的发射 `DirectEvalSpread`；VM 在执行期比较被调函数与内建 `%eval%`，
  相同才走直接 eval（被遮蔽时回退普通调用）。
- **运行时管线**：`Context::evalSource` 依次执行 Parser(Script) → Sema
  （`SemaOptions::inheritedStrict` 继承调用方严格模式）→ Compiler
  （`CompilerOptions::eval/evalStrict`）→ VM。
- **作用域模型**：编译产物入口 `pushScope` 建立 eval 词法作用域；sloppy 直接 eval 的
  var/函数声明经 `DeclareVar` 进入调用方变量环境（`currentFunctionEnv_`），let/const
  留在 eval 词法作用域；严格 eval 的 var 也不外泄。间接 eval 在 global 执行。
- **完成值**：表达式语句（含块/if/循环内）执行后写入隐藏槽 `*eval-completion*`，
  由 `ret` 返回；嵌套函数体与 static block 关闭该行为。
- **%eval% 形态**：不可构造、无 `prototype` 属性（`FunctionCreateOptions`）。
- **词法冲突**：sloppy 直接 eval 的 var 声明若与调用方词法绑定同名，
  `declareVar` 在 current → 变量环境之间检测到直接绑定并返回 SyntaxError。
- **Code 生命周期**：eval 内创建的函数可能逃逸并长期引用字节码，故 eval 的 `Code`
  由 `Context::evalCodes_` 持有到 Context 析构。
- **闭包作用域所有权**：FunctionData 对 closureScope 持有一个 `captureRef` 所有者 pin。
  析构时先释放所有者 pin；若仍有其他 Environment 闭包链引用，则标记 autoDispose 并
  脱离作用域树，由最后一个引用者回收。这样 GC 任意析构顺序下都不会释放仍被后代
  闭包链引用的 parent 作用域。

当前限制：直接 eval 的调用点识别只看语法上的 `eval(...)`，实际是否直接 eval 由
运行时函数同一性判定；完成值对 try/finally 等复杂控制流按「最后执行的表达式语句」
近似，未实现完整的 Completion Record 传播；eval 内函数对调用方后续新增绑定的
under-capture 依赖 Environment 链，已由 dynamic 标记覆盖。

## 上下文快照：SourceType::Eval + SemaOptions（2026-11-17）

eval 源码不是独立 Script，其 Sema 早期错误判定依赖调用方上下文
（§19.2.1.1 step 10/11）。为避免把 `IContext*` 直接传入 Sema/Compiler，
引入只读上下文快照：

- **`frontend::SourceType::Eval`**：Parser 标注 eval 源码；Compiler 据此走
  `compileEvalProgram`，与 `CompilerOptions::eval` 共同作为入口开关。
- **`SemaOptions` 扩展**：`evalContext`、`allowsNewTarget`、`allowsSuperCall`、
  `allowsSuperProperty`、`allowsArguments`。`ControlFlowPass` 在
  `evalContext` 下用这些字段构造顶层 `Context`，替代 Script/Module 的硬编码。
- **快照来源**：`Context::evalSource` 在直接 eval 时从运行时 `currentFunction_`
  提取 `FunctionData`/`ClassData`：`new.target` 恒可继承；`super()` 仅派生类
  构造器；`super.prop` 要求 `homeObject != nullptr` 且非基类构造器。
  间接 eval 不继承（在 global 执行）。
- **严格性**：`inheritedStrict`（直接 eval 查调用方 scope flag 链）或源码自身
  `"use strict"`，同时传给 Sema 与 Compiler。

该快照是 eval 特例的临时 Env 回退：纯数据、可测试，不把宿主上下文耦合进
Sema/Compiler。私有名可见集、arguments 对象、全局词法冲突与完整
Completion Record 仍待后续补齐。

### 私有名可见集（2026-11-17）

直接 eval 继承调用方 PrivateEnvironment（§19.2.1.1 step 10）。`CompilerOptions`
新增 `privateEnvironment`（名字 → field/method/getter/setter），`findPrivateInfo`
在 class 体作用域栈未命中时回退到该表。`Context::evalSource` 在直接 eval 时从
`currentFunction_->ownerClass`（`ClassData`）还原实例/静态私有字段、私有方法与
访问器，使 `eval('this.#x')`、`eval('#x in this')`、`eval('this.#m()')` 可编译并
经既有 VM 私有指令执行。

限制：只取当前函数所属 class 的私有环境；嵌套 class 中引用外层 class 的私有名
仍不可见（静态编译路径同样受运行时 `ownerClass` 单层限制）。

### catch 参数同名 var（Annex B.3.4，2026-11-17）

简单 catch 参数（标识符）作用域由 Compiler 发射 `mark_catch_scope` 打上
`"catch"` 标记。`Context::declareVar` 在扫描词法冲突时跳过该标记作用域：
eval 的 `var e` 仍建在变量环境，但初始化器经正常 `store` 写回 catch 参数
（符合 Annex B「不遮蔽 catch 参数」）。解构 catch 参数不标记，仍报
SyntaxError。

### 块级函数声明（Annex B.3.3，2026-11-17）

非严格模式下，块、`if` 子句与标签语句中的函数声明除块级绑定外，还必须在
变量环境中建立同名 `var` 绑定，并在该声明执行时同步赋值：

- **严格性跟踪**：Compiler 新增 `strict_`，由 Script 的 `"use strict"` 指令、
  eval 的继承严格性、Module 恒严格三处初始化，并在函数体编译时保存/恢复。
- **变量槽收集**：`hoistVarSlots` 在非严格模式下额外用
  `collectAnnexBFunctionNames` 递归收集块/if/循环/try/catch/switch/labeled 中的
  函数名（跳过函数/类边界，顶层函数不重复加入），在函数/模块/eval 变量环境建槽。
- **块入口赋值**：块级 `hoistFunctions` 在非严格模式创建函数对象后，经
  `load + store_var` 同步写外层变量绑定。`if` 子句函数按合成块处理，在分支
  执行时创建并赋值；标签函数按 `var` 语义直接在当前位置赋值。
- **变量环境定位**：新增 `push_var_scope` 指令，函数体作用域带 `"var-scope"`
  标记；`Context::storeVar` 优先用 eval 变量环境，否则沿作用域链找最近的
  `"var-scope"`（跳过块/with/catch 等词法作用域），最后回退
  `currentFunctionEnv_`/root。这样块级绑定仍独立存在，闭包捕获的是块级槽位，
  而外层 `var` 只观察赋值结果，符合 Annex B 的双绑定语义。

### 全局词法环境与 var 复用（2026-11-17）

间接 eval 必须在脚本全局环境中执行，能同时看到全局对象记录（内建/var）与
全局词法记录（顶层 let/const）。此前间接 eval 入口取 `global_`（仅内建），
看不到脚本顶层 var/let/const。修正：

- **脚本顶层环境固定**：Context 新增 `scriptRoot_`，构造时指向 root 且永不随
  `setRootScope` 切换。间接 eval 的词法环境入口改为 `scriptRoot_`，沿 parent
  链仍可达 `global_` 内建；间接 eval 的变量环境也改为 `scriptRoot_`，使其
  `var` 与脚本顶层 `var` 同槽复用，而不是在 `global_` 另建一份。
- **var 绑定标记**：`Scope::Binding` 新增 `varBinding`，`define`/`Define`
  指令新增对应 flag。var 声明、顶层函数声明与形参在建槽时标记为变量环境绑定。
- **冲突判定**：`declareVar` 扫描调用点到变量环境之间的绑定时跳过
  `varBinding`（同名 var/函数/形参可复用）；变量环境内已有同名词法绑定时
  返回 SyntaxError。这样 `function f(){var x; eval('var x=2')}` 与
  `let x; eval('var x')` 分别得到复用与早期错误。

已知限制：全局内建（如 `Math`）通过 `bind` 落在 `global_`，间接 eval 的
`var Math` 仍会在 `scriptRoot_` 建遮蔽绑定，未按全局对象属性复用。

### 完成值：try/catch/finally（2026-11-17）

完成值仍用隐藏槽 `*eval-completion*` 近似，但补齐了 try 语句的 Completion
Record 组合语义：

- **catch 入口清空**：catch 只在异常路径进入，try 体抛错前的完成值作废；
  catch 入口把完成值槽置 undefined，catch 内表达式语句再更新。
- **finally 丢弃自身值**：finally 入口用 `load_finally_completion` 把待保留的
  完成值压栈并克隆（完成值槽是活绑定，store 会原地改写同一 Value，必须存副本）；
  finally 正常结束时写回。若入口有 pending 完成（异常/break/continue），
  UpdateEmpty 视为 undefined，压 undefined。
- **finally 内 break/continue**：编译期跟踪 `finallyDepth_`，在 finally 内的
  break/continue 前把完成值槽清为 undefined；若该跳转仍留在 finally 内，
  finally 正常结束的写回会覆盖清除，语义仍正确。

新增指令 `load_finally_completion`（ADR-024）。限制：完成值仍是「最后执行的
表达式语句值」的槽位近似，未建模每个语句的 Completion Record 结构本身；
switch/循环等路径依赖表达式语句写入槽位，暂未发现可观察偏差。
