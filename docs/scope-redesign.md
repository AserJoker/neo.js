# 变量作用域体系重做设计（scope-redesign）

> 版本：v0.2（草稿，待用户确认）
> 日期：2026-10-30
> 状态：**设计确认中**
> 关联文档：`docs/architecture.md`、`docs/gc.md`
> 重做原则：**除 AST 外完全重写**，旧 scope/opcode/env 全部废弃，测试按新语义重写

---

## 0. 背景与动机

当前变量作用域体系（栈槽局部变量 + JSEnvironment 环境链 + 块环境 + with 对象环境渐进叠加）语义交错复杂、难以维护，with 重做时暴露深层设计缺陷。用户判定需要**推到重来，做完整设计**。

**重做原则：除 AST 外完全重写，不迁就任何旧机制**（旧 scope/opcode/env 全部废弃，测试按新语义重写）。

本设计是重做地基的蓝图。核心思想：

- **value 是值语义实体，data 是引用计数共享体**；变量重复引用仅发生在闭包捕获与模块 import 两处
- **scope 持有 values + named_values**，作用域树代替栈变量，**栈只用于操作数**
- **vm 按次独立（每次模块执行/函数调用），context 共享**；跨 vm 边界只传 value/exception
- **interrupt 机制统一处理控制流**（return/throw/break/continue/label/异步挂起），try_frame + label_frame 栈配合
- **scope_ref 引用计数**决定 value 生死，GC 只负责值间引用图
- **字节码是运行/编译分水岭**：编译信息（shadow_value 等）不流到运行期

---

## 1. 核心抽象

### 1.1 value 与 data 分层

- **value**：值语义实体。return/传参/赋值都产生**新 value**（拷贝语义）。
- **data**：引用类型的实际数据（对象/函数/数组）。新 value 指向**同一份 data**，data 用**引用计数**管理复用。
- **变量重复引用仅两处**：闭包捕获（closure scope 与原 scope 共享）+ 模块 import（两模块 scope 共享）。

### 1.2 scope（作用域）

每个 scope 持有：

| 成员 | 说明 |
|------|------|
| `values`(vec) | 当前作用域持有的 value 清单（**所有权**） |
| `named_values`(map) | 具名 value 的引用（**所有权在 values**） |
| `parent` / `children` | 作用域树（词法查找沿 parent 链） |

- **作用域树**代替栈变量；**栈只用于操作数**。
- `delete scope`：先从 parent 摘除自己 → 递归销毁所有 children → 遍历 values 逐个 `scope_ref--`。
- **value 的所有权永远在 scope 中**；栈中存的全部是借用值，不需要 GC。

### 1.3 函数调用模型

函数对象持有 **closure scope**。作用域链：

```
closure scope → argument scope → body scope
```

- closure scope 的父 = 定义模块的 global scope → 跨模块全局注入自然解决。
- 每次调用重建 argument scope（参数定义进去，含 this）+ body scope；`ctx->current_scope = body scope`。

### 1.4 全局结构

- **rootScope** 是全局根，所有模块的 globalScope 挂在 rootScope 下。
- 全局对象在 rootScope 里。
- this 不是永生的：就是 **argument scope 中的一个变量**（调用时确定）。

---

## 2. var/let/const 声明语义

| 声明 | 提升目标 | 创建时初值 |
|------|---------|-----------|
| `var` | 函数/模块作用域顶层 | `undefined` |
| `let`/`const` | **当前（块）作用域顶层** | `UNINITIALIZED` 哨兵值 |

- 作用域进入时统一创建绑定（var=undefined、let/const=UNINITIALIZED）。
- 声明处实际赋值 → **TDZ 靠哨兵自然解决**（读 UNINITIALIZED 抛 ReferenceError），不需要运行时逐条检查。
- 保留块作用域：`{let x} {let x}` 重名、块内遮蔽、for per-iteration 闭包语义正常。

---

## 3. 操作数全部进 scope 跟踪

- 每个新 value 创建时必须加入 `ctx->current_scope->values`（如 `create_number`）。
- 栈只放 value 指针，**不存在孤立 value**。
- 临时 value（如 `create_number(123)`）也在当前 scope 跟踪，随当前作用域退出 GC。

---

## 3.5 变量读写语义：load / store / clone（定稿，2026-10-30）

### load：返回原始变量

- `load "a"` 返回 `a` 的**原始变量**（value 本身，不是拷贝）。
- 这样 `a` 加入闭包 scope 时，闭包 scope 引用的是**同一个 value**（scope_ref++）→ 读写穿透，闭包看到最新值。

### store：改 data，不顶替变量

```
store "a" 123：
  a.data = clone(create_number(123).data)   // 123 的装箱 value 的 data clone 给 a 的 data
  // a 的 value 对象本身不变，变的是 a 的 data 内容
  // 不是让 a 指向新 value
```

- **store 不顶替变量名字**，只改 data 内容 → 闭包 scope 持有的同一 value 读到新 data。

### clone：按 data 类型分派

```
clone(data):
  case 复杂变量（引用类型，有 refcount）:
    data.ref++;          // 引用计数 +1，共享同一 data
    return data;         // 返回同一 data（浅共享）
  case 小对象（内联在 value 里）:
    return copy(data);   // 直接拷贝值
  case 其他（值类型/需深拷贝）:
    return deep_copy(data);  // 深拷贝，new 新 data，赋值
```

### 临时 value 生命周期

- 临时 value 在当前作用域中跟踪（scope_ref=1），store 只把其 data 内容/引用交给目标变量，临时 value 本身生命周期照旧，随当前作用域退出 GC。

---

## 4. GC：value 生命周期管理（与内存管理解耦）

### GC 职责收窄

- **GC 只管 value 的生命周期**（scope_ref 归零 → delete value），不跟踪底层内存块。
- 每个 value 带 `scope_ref`（被多少作用域持有）；作用域退出遍历 values 逐个 `scope_ref--`；归零才可回收。
- GC 时 `scope_ref > 0` 的 value 当根跳过。
- **严格配对必须**；value 可被多 scope 共享。
- **函数返回值过渡**：旧 value 归零释放；调用方 `create_value(res)` 重建新 value 引用同 data。

### 内存管理：分配器（与 GC 解耦）

- **底层内存块归分配器（Allocator）管，不归 GC**；当前直接复用 `malloc/free`。
- **分配器是纯类对象（非实例）**，`alloc/free` 等接口都是 **static**——适配 C++ `operator new/delete` 重写。
- 内不内联小值 GC 不关心，**没有内联小值问题**；create_value 开销可接受，**全部装箱**。

### data 生命周期归 value 析构

- **data 的生命周期归 value 的析构函数管**：value 析构时决定怎么释放自己的 data。
- 释放策略（value 多态/自描述）：直接 delete（简单对象）/ 小对象不处理 / 复杂对象引用计数（value 持有 refcount，析构时 --，归零 delete）。
- 引用计数增减发生在 value 拷贝/赋值时（create_value ++，析构 --）。

---

## 5. 闭包：隐式捕获 → 显式捕获

- 编译期 sema/dry-run 扫描函数体识别每个标识符来源，闭包变量作为函数编译产物（**捕获表**）。
- 函数对象持有 closure scope；定义时把捕获的变量挂上去（**copy 绑定关系，不 copy 值**——共享最新值，符合 JS 语义）。
- 作用域链：closure scope → argument scope → body scope。

---

## 6. vm 生命周期

- **context 共享长寿命**：rootScope/模块表/堆/句柄表/内置。
- **vm 每次模块执行/函数调用独立**：操作数栈/try_frame 栈/label 栈/current scope 链都是"一次逻辑执行"的临时数据，不跨函数/模块。
- **跨函数 throw**：被调函数返回 `exception(value)`（普通值，快速失败）→ 外层 vm 调用后检查返回值是否 exception → 重包 interrupt 压栈 → 当前 vm 处理。
- **通用规则：一切返回 value 的接口，调用后都检查返回值是否是 exception**。
- interrupt 不区分 return/throw：值是否 exception 就区分了（**throw 就是值类型为 exception 的 return**）。

---

## 7. 函数调用边界（调用序）

```
scope_t current = ctx.current_scope;       // 保存调用方作用域
ctx.current_scope = func.closure_scope;    // 切到函数闭包作用域
ctx.push_scope();                          // argument scope
scope_t arg_scope = ctx.current_scope;
<init arguments>                           // 参数定义进 argument scope（含 this）
ctx.push_scope();                          // body scope
value_t res = func.call(ctx);              // 被调函数独立 vm，执行完返回
ctx.current_scope = current;               // 恢复调用方作用域
res = ctx.create_value(res);               // 重建 value（拷贝语义，data 复用）
delete arg_scope;                          // 销毁 argument scope
if (res.kind == EXCEPTION) {
    res = ctx.create_interrupt(res);       // 异常：包 interrupt 压栈
}
vm.push(res);                              // 压栈（正常返回值或 interrupt）
```

特殊值 COROUTINE 时**不销毁 argument scope、不销毁 vm**，只 `ctx->current_scope = current`（见 §9 异步）。

---

## 8. 退出机制：interrupt + try_frame + label_frame

### 8.1 interrupt（特殊值）

- **interrupt**：特殊 kind，data 携带 { 内部 value, 控制流 kind: return/break/continue/yield/await, 可选 label }。
- `op_ret` = `pop → create_interrupt(return, val) → push`，不做别的。
- **VM 每执行完一个操作码后查栈顶是否 interrupt → 统一消费**（return/throw/break/continue/label/异步挂起都走这一个口）。
- **中断传播期间不平衡作用域**！interrupt 里的 value 仍被当前作用域持有（scope_ref 不递减），不悬垂；平衡推迟到"消费点"。
- interrupt 也进 scope 跟踪，但**不递归销毁自己 data->value**（data->value 是借用，所有权在 scope）。

### 8.2 try_frame 栈（vm 持有）

每帧记录：catch 地址 / finally 地址 / 块结束地址 / 进入时 scope / 进入时栈顶。

- compiler 生成 `op_push_try_frame` / `op_pop_try_frame`。
- 异常：取 interrupt 的 value 压栈 → 恢复 scope + 栈顶 → 跳 catch 地址。
- catch 结束 → 抛 "catch finish" interrupt；finally 结束 → 抛 "finally finish" interrupt；VM 捕获后平衡 try_stack/scope 链/操作栈 → 按 try_frame 记录结束地址续跑。
- **防重入**：进入 catch 前先把该帧 catch 地址置 0 → catch/finally 内再 throw 时该帧无 catch，沿 try_frame 链向上找，中间每帧都执行 finally。

### 8.3 label_frame 栈（vm 持有）

每帧记录：`start`（continue 位置）/ `end`（break 位置）/ 可选 label / 当前作用域 / 栈顶。

- compiler 生成 `push_label_stack` / `pop_label_stack`。
- `break` → 查 label 栈找到对应帧 → 跳到 `end`；`continue` → 跳到 `start`。
- 普通（非 try）break/continue：直接平衡 label 栈 + 作用域 + 操作数栈（label 栈无 finally 副作用，直接平衡）。

### 8.4 label_frame 与 try_frame 相互处理

**break/continue 跳出时，中间经过的 try 块必须平衡（执行 finally）**：

- 从当前位置到目标 label 之间，所有被跨越的 try_frame 都要走 finally 逻辑，然后才跳转。
- 作用域 + 操作数栈逐层平衡，最后才跳到目标 start/end。
- 这跟 throw 传播是同一套"中断消费"机制：VM 统一消费者沿 label 栈找目标帧，沿途对每个 try_frame 执行 finally。

### 8.5 finally 覆盖规则

**finally 中的控制流会覆盖原有的 break/continue/return/throw 动作**：

```
label: for (...) {
  try {
    break label;      // ① 跳出意图
  } finally {
    continue label;   // ② finally 里的 continue 覆盖 ①
  }
}
```

- interrupt 是"单一槽"而非"栈"：正在处理的 interrupt 被存下，进入 finally 执行；finally 里产生新中断 → **新中断覆盖旧中断**（旧意图丢弃）。
- finally 结束时若无新中断 → 恢复旧中断继续传播；若有 → 用新中断替代。
- 旧中断的 value 所有权在源作用域，覆盖只是换载体，不会悬垂。
- **finally 是唯一的"覆盖点"**：其他传播（跨 try_frame 跳转、平衡作用域）不改中断内容。

---

## 9. 异步挂起/恢复（协程统一）

- **挂起 = interrupt 变体**：异步点（yield/await）压 interrupt，kind=yield/await；内含 value 是特殊值 **COROUTINE**（与 EXCEPTION 同为特殊值）。
- **generator/async 共用同一协程现场**：挂起原因（yield/await）记录在 InterruptData.kind，调度方式（generator.next vs promise settle）由持有者对象决定——协程 value 本身不区分两者。
- **COROUTINE 里打包整个 vm**（操作数栈/scope 树/try_frame/label 栈/pc）。
- **挂起边界不销毁现场**：call 遇到 COROUTINE 时**不销毁 argument scope、不销毁 vm**，只 `ctx->current_scope = current`（恢复调用方 scope）。scope 树可持续持有、value scope_ref 保持 >0，无损。
- **恢复 = 直接复用 vm**：generator 对象/promise continuation 持有该特殊值，恢复时取出 vm 直接复用，从挂起点继续（不重建栈/scope）。
- **vm 与 GC 解耦**：stack 中存的全部都是借用值，不需要 GC；value 所有权永远在 scope 中。COROUTINE 也是 value，在 current_scope 中创建（call 遇到时安全：不删 argument scope 且恢复 current_scope）。
- **generator vs async 差异（外部持有者职责，非 value 属性）**：
  - generator yield 挂起 → 特殊值返回给调用方（next() 拿 {value,done:false}），vm 存 generator 对象。
  - async await 挂起 → 特殊值不返回给调用方（调用方已拿 promise），由 async 调度层（微任务）持有，promise settle 后 resume 同一 vm。
- **挂起 vm 的 GC 可达**：COROUTINE 被 generator/promise 持有 → GC 从它沿 特殊值→vm→scope 树→values 递归标记/保护；挂起时 scope 树从"vm 临时数据"提升为"GC 可达根"，恢复后再降回。

---

## 10.5 with 特殊 scope（已定稿，2026-10-30）

with 是**废弃语法**（报 deprecation warning），实现从简。

### 结构

```
with scope（特殊 scope 节点）：
  - named_values：块内 let/const 声明（遮蔽优先）
  - obj：with 对象引用
  - parent：外层作用域
```

### Load/Store 解析链（与普通作用域一致，从内到外）

```
1. named_values（块内 let/const）→ 命中写/读块级绑定
2. obj 属性（实时 HasProperty）→ 命中写/读对象属性
3. 向上查找外层绑定
4. 未找到：load → ReferenceError；store → 非严格隐式全局 / 严格 ReferenceError
```

### 语义要点

- **store 与 load 走同一条链**：属性存在 → 写 obj 属性，不落到外层 var（§9.4.6.5 SetMutableBinding 命中即返回）；属性不存在 → 才向外。
- **动态属性增删**：实时查 obj 属性，天然支持（`with(o){o.x='added'}` 后 `x` 可解析；`delete o.x` 后回落外层）。
- **不实现 @@unscopables**（废弃语法，不处理）。
- **嵌套函数捕获**：sema 标记 with 区内标识符为"动态捕获"，closure scope 挂 with scope 引用，运行时穿透查 obj 属性（编译期无法预知 with 对象属性，不能静态枚举）。

---

## 10.6 eval（已定稿，2026-10-30）

### 直接 eval（复用当前 vm）

Node v24 实测：

| 声明 | 直接 eval 行为 |
|------|--------------|
| `var` / 函数声明 | 进**当前函数级**作用域，覆盖当前函数局部（`eval('var x=1')` 后局部 x=1） |
| `let`/`const` | **只在 eval 代码内可见**，外面访问 ReferenceError；多次 eval 同名 let **不冲突** |
| TDZ | eval 内 `let b` 声明前访问 → ReferenceError（哨兵值机制照常） |
| 函数声明 | 进当前函数作用域，函数外不可见 |

### 设计

```
直接 eval = 复用当前 vm（同一逻辑执行内继续执行）：
  - 在 current_scope 下新建一个 eval 块 scope
  - var/函数声明 → 提升到 current_scope 的函数级（var 层）
  - let/const → 提升到 eval 块 scope 顶部（UNINITIALIZED 哨兵，TDZ 照常）
  - 执行完 eval 块 scope 销毁（其 values scope_ref--），var 声明的留在 current_scope
  - throw 沿当前 vm 的 try_frame 链传播（eval 内 throw 能被外层 try 捕获）
间接 eval / (0, eval)() → 全局作用域执行（新 vm？待定）
```

---

## 10.7 模块（已定稿，2026-10-30）

### 关键修正：scope 与 vm 解耦

- **scope 与 vm 解耦**：vm 只是"解析字节码、操作 ctx"的执行器，不拥有 scope。scope 树归 ctx（context 级），vm 只引用/操作它们。
- **模块 globalScope 常驻**：rootScope 的子 scope，随 ctx 销毁而销毁（模块缓存，不随模块 vm 结束销毁）。
- **模块间平等**：都是 rootScope 的 children，无父链关系，靠显式 import。

### import = 运行时 copy 绑定关系（两 scope 共享同一 value）

```
// a.js
export let count = 0;        // count value 在 a 模块 globalScope 的 values 里

// b.js
import { count } from './a.js';   // 运行时：b 的 import 绑定 = 引用同一个 value

a 模块 globalScope.values  →  [count_value]       scope_ref++
b 模块 globalScope.named_values["count"] → count_value（同一指针）scope_ref++
```

- **同一 value 被两个 scope 引用 → scope_ref++**（import 是第二个允许"变量重复引用"的场景，与闭包并列）。
- 读：两个 scope 读到同一 value → 同一 data → **live binding 自然成立**。
- 写：写 value->data，两个 scope 看到的 data 都变 → 最新值。
- **import 无只读要求**（不需要 TypeError 检查）。
- value 本身不变，变的是 value->data。

### 模块执行

- current_scope 直接用模块 globalScope（复用常驻 scope，不新建）。
- 模块体内块 scope 是 vm 内临时 scope，vm 结束销毁；模块 globalScope 保留。

---

## 10.8 编译期：独立 sema 阶段（已定稿，2026-10-30）

### 定位

- 独立阶段：**AST 生成后、compile 前**。
- **保守**：只模拟作用域和变量关系，不校验真实来源（`import {x} from "./a.js"` 不校验 a.js 是否真的导出 x，默认当前模块全局有一个 x）。
- 这是"收集变量提升"的基础。

### 核心机制：shadow_scope + shadow_value

- 遍历语法树，**模拟真实的作用域创建销毁、变量定义、标识符查找**。
- 对每个标识符访问，**查找它的词法来源**。
- 产出的挂载：
  1. **shadow_scope + shadow_value** 挂到"产生作用域的 AST 节点"上。
  2. **定义点引用**挂到所有 identifier 访问节点上（定义该标识符的 AST）。
  3. 定义点和访问点**穿越函数 scope** 时 → 名字加进函数 AST 的**捕获列表**。

### shadow 执行

- 异步/生成器**全部当成线性执行**；`await/yield/return/throw/break/continue` **不发生中断**。
- 每个 scope 线性执行完成。
- 只关心：作用域创建销毁、变量定义和使用。

### shadow_value 与运行时分水岭

- **shadow_value 纯编译期**，只给 compiler 参考。
- **字节码是运行/编译分水岭**：编译信息不流到运行期。

### 捕获列表 + op_set_closure

- 捕获列表只要**名字**（编译期产物，函数 AST 上）。
- 运行时生成：
```
op_set_closure "aaa"
  → value_t func = vm.top();
  → value_t val = ctx.load("aaa");     // 运行时沿 scope 链查
  → func.set_closure("aaa", val);      // 挂到函数 closure scope
```

### 多层闭包：编译-运行不对称（设计目的）

```
function outer() {
  let a = 1;
  function middle() {
    function inner() {
      return a;    // ① 语义上：a 来自 outer
    }
  }
}
```

- **sema（编译期）**：inner 引用 `a` → 沿 shadow 链向上找 → 定义在 outer → 登记 `inner.捕获 = {a}`；穿过 middle 函数作用域 → **逐层登记 `middle.捕获 = {a}`**。语义上 `a` 的定义点标注为 outer 的 `let a`。
- **运行时**：inner 的 closure scope 捕获 `a` → **实际从 middle 的 closure scope 取**；middle 的 closure scope 捕获 `a` → 从 outer 取。形成 closure scope 链：`inner.closure → middle.closure → outer.body/closure`。
- **编译-运行不对称是设计目的**：编译时认为 inner 的 `a` 捕获来自 outer 定义，运行时 inner 的 `a` 实际来自 middle 的捕获——**每层 closure scope 都捕获传递，多层词法作用域自然穿透**，无需运行时跳多层查链。

### import 的 shadow 建模

- `import {x} from "./a.js"` → 在当前 shadow_scope 中定义 `x -> {import AST}`。
- 编译遇到该标识符 → 知道是 import 绑定 → 生成运行时 import 捕获（两 scope 共享 value）。

### 变量提升（shadow 记录）

- 遍历完成每个 shadow_scope 记录该 scope 的所有变量（定义语句将自己加入 shadow_scope）：
  - **var**：向上查找到**函数/模块级别**（提升）。
  - **let/const**：找**当前** scope。

### 变量提升在 sema 侧消减（优化）

- compile **不需要在定义点做提升**。
- 当前 AST 节点挂有 shadow_scope 时：
```
compile 遇到产生作用域的 AST 节点（挂有 shadow_scope）：
  1. PUSH_SCOPE（创建运行时 scope）
  2. 遍历该 shadow_scope 的 shadow_value，逐个定义：
     - var → undefined
     - let/const → UNINITIALIZED 哨兵
  3. 继续编译 body
  4. 遇到定义点（声明语句）→ 只剩一个赋值操作（把初始值写进已定义的绑定）
```
- **提升语义完全在 sema 侧消减**：shadow_scope 记录的"该 scope 有哪些变量"就是运行时 scope 进入时要创建的完整清单，compile 照做即可。
- TDZ 更自然：作用域进入时 let/const 已是 UNINITIALIZED 哨兵，定义点之前任何访问读到哨兵 → ReferenceError。

### for 多层 scope（已确认）

- **for-init 和 for-body 是两个独立 shadow_scope + 运行期 scope**（本来就应该遮蔽）。
- for-body 的 let/const 定义在 for-body scope，遮蔽 for-init 的同名变量。
- **per-iteration 闭包**：for-init scope 每次迭代重建（每次迭代新 value，值拷贝），闭包捕获当次迭代的 value → `for (let i=0;i<3;i++) arr.push(()=>i)` → `[0,1,2]`。
- 运行时：
```
for 循环头 → PUSH_SCOPE（for-init scope）
循环：
  PUSH_SCOPE（for-body scope，每次迭代重建）
  body 执行（闭包捕获当前迭代的 for-init value）
  POP（for-body scope 销毁）
  更新 i++（复制当前 i 的 value 到新 for-init scope）
循环结束 → POP（for-init scope 销毁）
```

### 全局标识符

- 找不到定义 → identifier 挂 `NULL`（标记全局）。
- 运行期**不需要特殊处理**：沿作用域链查找**最终必然到达全局**（rootScope），全局变量存在就 load 成功，不存在就是 ReferenceError。

### sema 完整流程

```
sema（AST 后、compile 前，独立阶段）：
  1. 遍历 AST，模拟作用域创建/销毁（shadow_scope 栈）
  2. 声明语句把自己加入 shadow_scope：
     - var → 向上查找到函数/模块级
     - let/const → 当前 scope
     - import → 当前 scope 定义 "x -> {import AST}"
  3. 每个 identifier 访问 → 沿 shadow 链查找定义：
     - 找到 → 挂定义点引用
     - 穿越函数 scope → 名字加入函数 AST 捕获列表
     - 找不到 → 挂 NULL（全局，运行时沿链查最终到 rootScope）
  4. 产出：
     - 产生作用域的 AST 节点 → shadow_scope + shadow_value
     - identifier 节点 → 定义点引用（或 NULL）
     - 函数 AST → 捕获名字列表
  5. 字节码是分水岭：shadow_value 只给 compiler 参考，不流到运行期

compile（消费 sema 产物）：
  - 标识符 → 看定义点：局部 / 捕获（op_set_closure 挂 closure scope）/ import / 全局
  - var 提升 → 作用域进入时创建（undefined / UNINITIALIZED 哨兵）
  - try/label 帧 → 生成 op_push_try_frame / op_push_label_stack

运行时：
  - 沿 scope 链查绑定，最终到 rootScope（全局）
  - 存在 → load 成功；不存在 → ReferenceError
```

---

## 11. 待定问题（需用户确认）

1. ~~**with / eval / 动态作用域**~~：with 已定稿（§10.5，特殊 scope + 实时查 obj 属性，不实现 unscopables）。**eval 已定稿**（§10.6，直接 eval 复用当前 vm + 新建 eval 块 scope）。
2. ~~**模块**~~：已定稿（§10.7）。
3. ~~**编译期**~~：已定稿（§10.8 sema 阶段）。
4. ~~**旧 JSScope / 旧 opcode / 旧测试**~~：**完全重写，除 AST 外不迁就任何旧机制**。旧 scope.h/scope.cc、旧 opcode、旧 env 机制全部废弃重写；测试按新语义重写。
5. ~~**性能取舍**~~：已定稿（§4）——GC 只管 value 生命周期（scope_ref 归零即 delete）；底层内存块归**分配器**（纯类对象 static 接口，当前 malloc/free）；data 生命周期归 value 析构；无内联小值问题，全部装箱。
6. ~~**GC 触发时机**~~：已定稿（§4）——scope_ref 归零即 delete value（无 mark-sweep，值间引用靠 data 引用计数）。
7. **实现顺序**：完全重写的里程碑拆分（sema → scope/value 运行时 → vm/interrupt → 闭包 → 模块/with/eval → 异步）。
