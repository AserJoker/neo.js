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
