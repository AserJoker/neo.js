# ADR-025: Sema 两遍 walk 与闭包穿透捕获

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-001（Sema 闭包收集）、ADR-013（闭包作用域）、ADR-014（提升/TDZ）、ADR-022（invoke）、ADR-024（字节码）

## 背景

确定 Sema 的具体流程：作用域树构建、变量提升、标识符解析、闭包捕获登记。
核心问题：如何让"函数孤立化"（ADR-013，运行时查找只需 closure→root→global，
不依赖定义时词法链）成立？答案是在 Sema 阶段完成**闭包穿透捕获**——
内层函数引用外层变量时，路径上每个被穿越的函数都登记捕获，使运行时
closure 链不断。

## 决策

### 1. Sema 至少两遍 walk

- **第一遍**：收集作用域与变量（建 shadow_scope 树 + 提升建槽）。
- **第二遍**：检查每个标识符的来源（沿 shadow_scope 向上查 + 闭包穿透登记）。

### 2. shadow_scope / shadow_value

Sema 内部数据结构，编译完丢弃，不进运行时。

- **shadow_scope**：挂在能创建作用域的 AST 节点上，表示一个作用域。
- **shadow_value**：作用域内的变量槽位（带名字、初值标记、可变性）。

### 3. 第一遍：建 shadow_scope 树 + 提升建槽

walk AST，遇创建作用域的节点挂 shadow_scope：

| AST 节点 | 挂的 shadow_scope |
|----------|-------------------|
| 函数定义 | `closure_scope`（持久，闭包变量）+ `argument_scope`（参数） |
| 函数体 | `function_scope`（= body_scope，局部变量） |
| for 循环 | `init_scope`（init 部分）+ `body_scope`（循环体） |
| 其他 block（if/while 体等） | `block_scope` |

遇变量定义语句，按声明类型找**最近的合适作用域**建 shadow_value：

| 声明 | 提升目标 | 初值 |
|------|---------|------|
| `var` | 最近的 function/module scope | undefined |
| `let`/`const` | 最近的 block scope | UNINITIALIZED（TDZ 哨兵） |
| `function` 声明 | 最近的 block scope | undefined（字节码三段式：先建槽 undefined，函数定义区 store 函数对象） |

第一遍结束：每个 AST scope 节点挂好 shadow_scope，每个 shadow_scope 里有
shadow_value 槽位（带初值标记）。

### 4. 第二遍：标识符解析 + 闭包穿透捕获

walk AST，对每个**标识符引用**沿 shadow_scope 链向上查：

- 找到 → 记录引用来源（指向该 shadow_value）。
- **关键**：若向上查找路径**穿越了函数作用域边界**（从内层函数引用
  外层函数作用域的变量），就在**被穿越的每个函数节点上登记捕获名字**。

#### 穿透示例

```js
const a = 1;            // module scope
function main(){        // main scope
  function fn(cb) {     // fn scope
    return cb(123) + a; // fn 引用 a
  }
  print(fn((arg)=>arg+1));
}
```

`fn` 内引用 `a`，沿 shadow_scope 向上查：

```
fn.body_scope → fn.argument_scope → fn.closure_scope
  → 穿越函数边界（fn）→ main.body_scope → main.argument_scope → main.closure_scope
  → 穿越函数边界（main）→ module scope → 找到 a
```

路径穿越了 `fn` 和 `main` 两个函数边界：
- **fn 上登记捕获 "a"**（fn 自己引用 a）
- **main 上登记捕获 "a"**（穿透：fn 引用 a 的路径穿越 main 边界）

**`main` 自己不直接用 `a`，但仍要捕获**——为维持 closure 链不断：
`fn` 定义时运行时 `ctx->load("a")` 沿 `main.body → arg → closure`（有 a）
找到；若 main 没捕获 a，链在此断开，fn 拿不到 a。

#### 穿透的本质

- **穿透不是 Sema 编译期传播捕获列表给父函数**，而是**查询路径的自然结果**：
  标识符向上查找，每穿越一个函数边界就在该函数登记捕获。
- **运行时 `set_closure` 实现简单**（ADR-024）：
  `val = ctx->load("name"); fn->bind_closure("name", val)`，
  沿当前 scope 链向上查，不区分目标是临时变量、外层闭包变量还是全局。
- **闭包穿透是"函数孤立化"的代价**：让运行时不依赖词法链，就必须在 Sema
  阶段把闭包变量逐函数登记，保证运行时 closure 链不断。

### 5. Sema 输出 → 编译器

Sema 输出每个函数的**捕获名字列表**。编译器据此在函数定义区发射
`set_closure "name"` 指令（ADR-024）。

```
函数定义区：
  push_function <label>
  set_closure "name1"     # 按 Sema 捕获列表逐个发射
  set_closure "name2"
  set_func_name "..."
  store "name"
```

### 6. eval / with 的处理

静态捕获在 `eval`/`with` 下会失效，按影响性质区分处理：

#### with — over-capture（可接受）

- `with(obj){...}` 块内自由标识符可能是 obj 属性或外层变量，Sema 无法静态确定。
- **保守处理**：with 块内的自由标识符**全部登记捕获**（多捕获）。
- 运行时 with 块内查找：先查 with 对象属性，再查 scope 链。
- **不影响闭包正确性**，仅捕获冗余，**不回退**。

#### 直接 eval — under-capture（必须回退）

- `eval(code)`（**直接调用**，非 `var e = eval; e(...)` 间接调用）可在
  **调用方作用域运行时注入变量**，Sema 静态看不见 → 捕获列表不完整。
- **回退 Environment 方式**：含直接 eval 的作用域不再依赖 Sema 的具名静态捕获，
  改为保留运行时 **Environment 链**（环境记录链表）：
  - 闭包捕获 **Environment 节点**（而非具名 slot）
  - eval 注入的变量进入当前 Environment 记录，沿链自然可见
  - 等于把原"搁置的 Environment 仅作访问器"（ADR-001）在 eval 路径下启用为兜底
- **间接 eval** 在全局执行，不注入调用方作用域，**无需回退**。

#### 对比

| | Sema 影响 | 性质 | 处理 |
|---|---|---|---|
| with | 误判（多捕获） | over-capture | 不回退，运行时 with 查找规则处理 |
| 直接 eval | 漏判（少捕获） | under-capture | 回退 Environment 链保护现场 |
| 间接 eval | 无（全局执行） | — | 不回退 |

## 后果

- 优点：Sema 两遍职责清晰（建树/解析）；闭包穿透捕获保证函数孤立化成立；
  运行时 `set_closure` 实现简单；with/eval 按影响性质区分处理。
- 缺点：Sema 第二遍需做穿透登记（路径上每个函数边界都登记，有开销）；
  eval 回退 Environment 增加运行时复杂度（仅 eval 路径）。
- 待定：shadow_scope/shadow_value 的具体数据结构、第一遍 walk 的具体顺序
  （前序/后序）、第二遍标识符解析的引用解析细节（如属性访问 vs 标识符）
  后续在 design/ 文档细化。
