# ADR-063: arguments 对象（mapped / unmapped）

## 背景

ES2024 §10.4.4 要求非箭头函数在调用时创建 `arguments` 对象。此前 Sema 已在
`FunctionArguments` 影子作用域声明 `arguments` 绑定，但运行时没有任何对象，
函数体读取 `arguments` 会落到全局查找。该功能与 eval 无关，独立实现。

## 决策

### 1. 类型与数据

新增 `TypeKind::Arguments`，`ArgumentsType` 派生 `ObjectType`，复用对象布局、
原型链与 GC 遍历。索引元素与 `length` 走既有的 `hasIndexedElements()` 能力
（与 Array / String 包装同一套内部方法分派），不新增中心化枚举分支。

`ArgumentsData` 字段：

- `Scope* scope`：函数环境（`currentFunctionEnv()`），映射读写目标；
- `Value* callee`：mapped 时的 callee；
- `std::vector<std::string> mappedNames`：索引 → 形参名，空串表示映射已解除；
- `uint32_t length`：实际实参个数。

### 2. mapped 语义

只有「非严格 + 简单形参列表（全为标识符）」才建立映射。索引读取由
`indexedElement` 实时查 `scope->lookupBinding(name)`，写入由
`defineProperty` 拦截后调用 `Context::storeBinding` 原地替换绑定槽位的
meta——与普通赋值共用 `cloneForNewValue + replaceMeta` 路径，不复制值。

`delete`、改为访问器、或收紧 writable/configurable 时解除映射，回落为普通
自有属性。`length` 是索引型虚拟属性（不可写/不可枚举/不可配置）；
`callee` mapped 为数据属性，unmapped 为抛 TypeError 的访问器；
`@@iterator` 指向 `Array.prototype.values`。

### 3. 作用域生命周期

arguments 可以逃逸（`return arguments`）。创建时对函数环境
`incCaptureRef()`：`popScope` 已按 pin 计数把该作用域转为 autoDispose 孤儿，
对象析构时释放 pin 并回收。`enumerateChildren` 主动 `visitScope`，使逃逸对象
成为环境的唯一 GC 持有者。协程析构同样尊重 pin 计数，避免销毁仍被引用的
执行作用域。

### 4. 编译期标记

`FunctionData` 增加 `needsArguments` / `argumentsMapped` / `paramNames`。
`push_function` 的 flags 增加 `kFunctionHasArguments` /
`kFunctionArgumentsMapped`；mapped 时紧跟 `set_param_names`（u32 count +
name 下标）记录形参名。非箭头函数（含方法、构造器）置 needsArguments；
箭头函数不创建，从而自然继承外层 `arguments`。

### 5. 严格性

`StrictnessPass` 在 `FunctionInfo` 标注 `isStrict`，Compiler 据此发射
`set_strict`（此前仅 class 方法/构造器发射）。该标注同时决定 arguments 是否
mapped，并补齐了运行时严格标记。

### 6. var arguments

`var arguments;`（无初始化）是 no-op：提升阶段已建槽，不能把隐式 arguments
对象重置为 undefined。Compiler 对无初始化的简单 var 声明不再发射 store。

## 后果

- 优点：复用对象模型与索引能力；mapped 与普通赋值共享绑定槽位语义；
  逃逸对象经既有 pin/autoDispose 机制保活，无需新根类型。
- 限制：形参重名（sloppy 简单列表）仍受 Compiler 建槽的重复定义限制，
  尚未支持；`arguments` 的 `Object.prototype.toString` tag 未单独定制。
