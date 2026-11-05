# ADR-034: 原型-构造函数序列与 new 操作

- 状态: 已接受 (2026-11-05)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-018（对象模型）、ADR-019（赋值语义）、ADR-021（Function 结构）、
          ADR-022（函数调用）、ADR-032（M3 阶段 5）

## 背景

ADR-032 阶段 5 完成原型链查找后，还缺 JS 对象创建的核心协作关系：
构造函数与原型对象的双向引用，以及 `new` 操作的完整语义。

## 决策

### 1. 函数创建时自动建立 prototype ↔ constructor 双向引用

`createNativeFunction` 在创建函数 Value 后，自动：

1. 创建一个普通对象作为 `fn.prototype`；
2. 在 `fn.prototype` 上定义 `constructor` 属性指回 `fn`。

属性描述符（与 ES 一致）：

| 属性 | writable | enumerable | configurable |
|------|----------|------------|--------------|
| `fn.prototype` | true | **false** | **false** |
| `proto.constructor` | true | **false** | true |

- 每个函数拥有独立的 prototype 对象。
- 存活：`fn` 的属性 value 被 `ObjectData::enumerateChildren` 遍历 → proto 保活；
  proto 的 `constructor` 属性同理保活 fn。双向引用由 GC 遍历自然处理，无需特殊逻辑。
- Sema/Compiler 无需生成额外字节码——创建函数即完成配置。

### 2. new 入口：IContext::construct 纯代理

```cpp
virtual Value* construct(Value* fn, size_t argc, Value* argv[]) = 0;
// Context::construct → fn->type()->construct(this, fn, argc, argv)
```

与 `call` 对称。`IType` 新增 `construct` 纯虚方法：

- **FunctionType**：完整 new 流程（见 §3）。
- **基础类型/Object**：返回 `Exception("<TypeName> is not a constructor")`。
- **Exception/Fatal/Uninitialized**：透传 self（不参与调用）。

### 3. FunctionType::construct 流程

```
1. 最少参数校验（与 call 共用 checkMinArgc）
2. proto = fn.prototype（沿原型链查找）
   非 Object/Function → 按 null 处理（无 Object.prototype 兜底，见 §5）
3. thisObj = 新建空对象；setPrototype(thisObj, proto)
4. res = invokeNative(fn, thisObj, argc, argv)   // 与 call 共用完整 invoke 流程
5. res 为 Exception/Fatal → 直接透传
6. res 为 Object/Function → 返回 res（构造函数显式覆盖）
7. 否则（原始值 / undefined）→ 返回 thisObj
```

- **this 强制为新对象**：忽略 ADR-022 §4 的 `FunctionData.self` 优先级。
  与 JS 一致——`bind` 后的函数被 `new` 时 this 仍是新对象。
- **复用 invokeNative**：作用域切换/恢复、返回值归属、栈平衡逻辑与 `call` 完全一致，
  抽成匿名命名空间辅助函数避免重复。

### 4. 返回值语义（完整 JS）

- 构造函数返回 **Object/Function** → 用返回值（允许构造函数替换 this）。
- 返回 **原始值**（number/string/boolean/null/undefined）→ 忽略，用新建的 this。
- 返回 **Exception/Fatal** → 透传（new 失败）。

### 5. 本阶段边界

- **无 Object.prototype 兜底**：`fn.prototype` 不是对象时，新实例的原型为 null，
  而非退化为 `Object.prototype`（后者尚未内建）。
- **无 `new.target`**：native 函数无法区分 call/construct 调用点。
- **无内建构造函数**：`Object`/`Array`/`Function` 等全局构造函数留待 stdlib 阶段。
- **无 `instanceof`**：需 `Symbol.hasInstance` 与原型链遍历，后续阶段。

## 后果

- 优点：`new` 语义完整（this 绑定/返回值/原型链）；构造函数与原型双向引用自动建立，
  无需用户代码手动配置；复用 invoke 流程，作用域与 GC 语义与普通调用一致。
- 缺点：每个函数多一个 prototype 对象（约 150 字节 + 属性容器开销）；
  无 `new.target` 使 native 构造函数无法做「必须用 new 调用」的检查。
