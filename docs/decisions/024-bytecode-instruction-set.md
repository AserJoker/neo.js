# ADR-024: 字节码指令集与栈契约

- 状态: 已接受 (2026-11-02)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-004（执行模型）、ADR-013（闭包作用域）、ADR-014（提升/TDZ）、ADR-021（Function 结构）、ADR-022（函数调用 invoke）、ADR-025（Sema 两遍 walk）

## 背景

确定字节码指令集的形态、模块组织方式、每条指令的栈契约，以及函数序言/调用约定
的完整规则。本 ADR 把此前在对谈中校准的字节码伪代码示例固化为正式设计。

## 决策

### 1. 模块字节码三段式组织

模块顶层字节码按固定三段式组织：

```
[1] 符号定义区    — 所有顶层声明的建槽（Sema 第一遍提升结果的落地）
[2] 函数定义区    — 所有顶层函数声明的实例化（push_function + set_closure + store）
[3] 源码逻辑入口  — 实际执行的语句序列
```

- **符号定义区**：统一 `push_<init>; define "name"` 模式建槽。
  - `var` → `push_undefined; define "name"`（初值 undefined）
  - `let`/`const` → `push_uninitialized; define "name"`（初值 UNINITIALIZED，TDZ 哨兵）
  - `function` 声明 → `push_undefined; define "name"`（先建槽为 undefined，
    函数对象在函数定义区 store 进来）
- **函数定义区**：`push_function <label>; set_closure ...; store "name"`，
  保证任何引用前 store 已完成。
- **源码逻辑入口**：实际语句；模块末尾 `hlt`。

**统一"先建槽后赋值"**：函数声明不搞"提升值=函数对象"特例，所有声明走同一路径
（先建槽 undefined，再在定义区 store）。函数初始槽是 undefined 无所谓，因为
逻辑区之前不可能访问到（详见 ADR-014 第 5 节推论）。

嵌套函数体以 `jmp <func_end>` 跳过，仅在被调用时执行：

```
jmp _func_end
_func_start:
  <函数体字节码>
_func_end:
```

### 2. 指令分类与栈契约

栈契约记法：`[before] → [after]`，栈顶在右。
`call N`/`ret` 等涉及 VM 切换的指令单独说明。

#### 2.1 栈操作

| 指令 | 栈契约 | 说明 |
|------|--------|------|
| `push_undefined` | `[] → [undefined]` | |
| `push_uninitialized` | `[] → [UNINITIALIZED]` | TDZ 哨兵 |
| `push_null` | `[] → [null]` | |
| `push_true` / `push_false` | `[] → [bool]` | |
| `push_number <n>` | `[] → [n]` | |
| `push_string <s>` | `[] → [s]` | |
| `pop` | `[a] → []` | 丢弃表达式语句结果 |
| `dup` | `[a] → [a, a]` | |
| `swap` | `[a, b] → [b, a]` | |

#### 2.2 变量操作

| 指令 | 栈契约 | 说明 |
|------|--------|------|
| `define "name"` | `[init] → []` | 在当前 scope 建槽，初值为栈顶，弹出 |
| `load "name"` | `[] → [val]` | 沿 scope 链向上查找（body→arg→closure→root→global） |
| `store "name"` | `[val] → []` | 沿 scope 链查找槽并赋值，弹出 |
| `set_mutable false` | `[val] → [val]` | 锁定刚 store 的槽为不可变（见 2.7） |

- `define` 的初值由前置 `push_*` 提供；Sema 已提升，源码定义点退化为 `store`
  （ADR-014 第 5 节）。
- `load`/`store` 均沿 scope 链向上，找不到 → ReferenceError。

#### 2.3 函数定义

| 指令 | 栈契约 | 说明 |
|------|--------|------|
| `push_function <label>` | `[] → [fn]` | 创建 script Function，kind=script，pc=label |
| `set_closure "name"` | `[fn] → [fn]` | `val = ctx->load("name"); fn->bind_closure("name", val)` |
| `bind_this` | `[fn, self] → [fn]` | `fn->data->self = self`（self 弹出），箭头函数用 |
| `set_func_name "name"` | `[fn] → [fn]` | 设置函数 name 属性 |
| `load_this` | `[] → [this]` | 取当前 this（ctx->self） |

- `set_closure` **运行时**执行：`ctx->load("name")` 沿当前 scope 链向上查找，
  把查到的 value 绑定到 fn 的 closure_scope（scopeRef++，共享非复制）。
  **不区分目标是临时变量、外层闭包变量还是全局**——查找链自然处理
  （ADR-013、ADR-025）。
- Sema 第二遍已生成每个函数的捕获名字列表，编译器据此发射 `set_closure` 指令。
- **箭头函数等价于定义点捕获外层 this 的普通函数**：`load_this` + `bind_this`
  在创建时把外层 this 焊死到 `fn->data->self`，字节码层不再区分箭头/普通
  （ADR-021 第 2b 节）。

#### 2.4 调用

| 指令 | 栈契约 | 说明 |
|------|--------|------|
| `call N` | `[fn, argv0..argvN-1] → [ret]` | 弹 fn + N args，self=undefined，调 invoke，压返回值 |
| `call_method N` | `[fn, argv0..argvN-1] → [ret]` | 同上，self=undefined（方法调用 this 由 fn.bind 决定） |
| `ret` | `[val] → []` | 函数返回，弹栈顶作为返回值，结束当前 VM |
| `hlt` | `[] → ∅` | 模块执行结束 |

**call N 栈契约（核心）**：

```
调用点栈（call 之前）：           invoke 内部新 VM 栈：
[fn]                              [self]         <- invoke 压入（undefined 或绑定的 self）
[argv0]                           [argv0]        <- invoke 逆序压入
[argvN-1]          call N  ⇒        ...
                                  (序言 StoreThis 消费 self)
弹出 N+1 个（fn + N args）
压入 1 个返回值
栈深变化：d → d - N（弹 N+1 压 1）
```

- **self 不在调用点栈上**：`call N` 隐式以 `self=undefined` 调 invoke。
- invoke（ADR-022）内部把 self 压到新 VM 栈顶，再逆序压 argv，
  序言 `StoreThis` 从新 VM 栈消费 self。
- 箭头函数：call 仍传 self=undefined，但 invoke 里 `func->data->self` 优先
  （绑定的外层 this）→ 压栈的是正确 this → StoreThis 取出正确 this。
  **统一调用约定，箭头/普通无特例**。

#### 2.5 控制流

| 指令 | 说明 |
|------|------|
| `jmp <label>` | 无条件跳转 |
| `jtrue <label>` | 弹栈顶，真则跳 |
| `jfalse <label>` | 弹栈顶，假则跳 |
| `jnull <label>` | 弹栈顶，null/undefined 则跳 |
| `<label>:` | 跳转目标标记 |

- statement 保证栈平衡（ADR-004），跳转发生在 statement 边界，栈深可预期。

#### 2.6 作用域

| 指令 | 说明 |
|------|------|
| `pushScope` | 创建 body_scope，parent=argument_scope（ADR-013） |
| `popScope` | 弹出 body_scope（函数退出时） |

- `pushScope` 在函数序言参数 store **之后**创建 body_scope。
- 局部变量（body 内 let/const）属于 body_scope；形参属于 argument_scope。

#### 2.7 this 绑定

| 指令 | 说明 |
|------|------|
| `store_this` | 弹栈顶 self，存入 `ctx->self`（函数序言消费 self） |

- **函数入口栈顶必是 self**（invoke 压入），序言 `store_this` 统一消费。
- 箭头函数也走 `store_this`（统一约定，无特例）：
  bind_this 在定义点已绑定外层 this 到 fn->data->self，
  invoke 优先用绑定值压栈，store_this 取出即正确 this。

### 3. 函数序言模板（统一）

任何函数（含箭头）入口序言：

```
_func_start:
  push_uninitialized; define "<param0>"    # 形参建槽（argument_scope）
  push_uninitialized; define "<param1>"
  ...
  store_this                                 # 消费栈顶 self → ctx->self
  store "<param0>"                           # 绑定实参（覆盖 UNINITIALIZED）
  store "<param1>"
  [RestToArray; store "<rest_param>"]       # 可选：rest 参数
  pushScope                                 # 创建 body_scope
  <函数体>
  push_undefined; ret                        # 编译器统一补隐式 return undefined
```

- **形参建槽**：`push_uninitialized + define`（建可写槽，初值 UNINITIALIZED），
  再 `store` 绑定实参（覆盖 UNINITIALIZED）。
- **StoreThis 先于 Store**：self 在栈顶，先消费。
- **pushScope 在参数绑定后**：参数属 argument_scope，局部变量属 body_scope。
- **函数末尾统一补 `push_undefined; ret`**：避免 VM 处理"函数体结束"特例。

### 4. const 不可变的字节码序列

```
push_uninitialized; define "a"     # 建槽（可写，初值 UNINITIALIZED）
push_number 1; store "a"           # 唯一允许的 store（覆盖 UNINITIALIZED）
load "a"; set_mutable false        # 锁槽不可变
```

- 槽必须先可写才能完成初次 store，`set_mutable false` **必须在初次 store 之后**。
- `const a;`（无初始化）在 Parser/Sema 阶段拒绝，不到字节码。

### 5. 完整示例

源码：

```js
const a = 1;
function main(){
  function fn(cb) { return cb(123) + a; }
  print(fn((arg)=>arg+1));
}
main()
```

字节码：

```
// [1] 符号定义区
push_uninitialized; define "a"
push_undefined;      define "main"

// [2] 函数定义区
push_function _main_start
set_closure "a"                       # main 捕获 a（穿透：fn 引用 a，路径穿越 main 边界）
set_func_name "main"
store "main"

// [3] 源码逻辑入口
push_number 1
store "a"
load "a"
set_mutable false                     # const a 锁不可变
jmp _main_end
_main_start:
  // main 序言
  store_this                          # main 无形参，仅 store_this
  pushScope                          # 创建 main 的 body_scope
  // main 符号定义区
  push_undefined; define "fn"
  // main 函数定义区
  push_function _fn_start
  set_closure "a"                     # fn 捕获 a
  set_func_name "fn"
  store "fn"
  jmp _fn_end
  _fn_start:
    // fn 序言
    push_uninitialized; define "cb"
    store_this
    store "cb"
    pushScope                        # 创建 fn 的 body_scope
    // fn 体
    load "cb"
    push_number 123
    call 1                            # cb(123)，self=undefined 隐式传
    load "a"
    add
    ret                               # 返回 cb(123)+a
  _fn_end:
  // main 体
  load "print"
  load "fn"
  // 箭头函数 (arg)=>arg+1
  push_function _arrow_start
  load_this                           # 取 main 的 this（外层 this）
  bind_this                           # 焊死到箭头函数 self
  jmp _arrow_end
  _arrow_start:
    push_uninitialized; define "arg"
    store_this
    store "arg"
    pushScope
    load "arg"
    push_number 1
    add
    ret
  _arrow_end:
  call 1                              # fn(arrow)，self=undefined
  call 1                              # print(...)，self=undefined
  pop                                 # 丢弃 print 返回值
  push_undefined
  ret
_main_end:
load "main"
call 0                                # main()，self=undefined
hlt
```

## 后果

- 优点：三段式模块组织统一所有声明的处理路径；call/序言栈契约清晰；
  箭头/普通函数字节码层无特例（统一 store_this）；定义点退化为 store 简化语义。
- 缺点：函数调用创建/销毁 VM 有开销（ADR-004）；箭头函数 bind_this 多两条指令。
- 待定：对象/数组字面量、try/catch（异常即值走 return 通道）、for 循环
  （init_scope/body_scope）、属性访问/方法调用的字节码细节后续 ADR 细化。
