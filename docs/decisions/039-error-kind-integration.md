# ADR-039: 引擎内部报错接入规范错误类型

- 状态: 已接受 (2026-11-06)
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-006（异常即值）、ADR-017（Exception/Fatal）、ADR-038（Error 体系）

## 背景

Error 体系（ADR-038）落地前，引擎内部报错统一是
`createException(createString("some message"))`——Exception 携带一个字符串，
没有错误类型、没有 stack、没有 `name`。共 83 处。

## 决策

### 1. 新增 ErrorKind 与 createErrorException

```cpp
enum class ErrorKind : uint8_t {
    Error, TypeError, RangeError, ReferenceError, SyntaxError, URIError,
};
const char* errorKindName(ErrorKind);

// IContext
Value* createErrorException(ErrorKind kind, std::string_view message,
                            const SourcePosition& position = {});
```

`createErrorException` 按 kind 创建对应 Error 对象（原型挂到该子类），
再按 ADR-006 包进 Exception 通道返回。**通道不变**：调用方仍只判断
`TypeKind::Exception`，只是携带的 child 从 String 变成了 Error 对象。

范围与 ADR-038 一致：EvalError 不在规范中，不实现；AggregateError /
SuppressedError 不在本阶段范围。

### 2. 内部辅助（TypeBehavior.h）

```cpp
inline Value* errorOf(IContext*, ErrorKind, const std::string&);
inline Value* typeError(IContext*, const std::string&);
inline Value* rangeError(IContext*, const std::string&);
inline Value* referenceError(IContext*, const std::string&);
```

避免各处重复写 `ErrorKind::`。`notAFunction` / `notAConstructor` /
`primitiveSetProperty` 等既有辅助内部改用 `typeError`。

### 3. 类型映射（按 ES2024 规范）

| 场景 | 错误类型 | 规范依据 |
|------|----------|----------|
| 非函数调用 `x()` | TypeError | §7.2.7 IsCallable |
| 非构造函数 `new x()` | TypeError | §7.2.7 |
| 数组长度非法 / 越界 | RangeError | §23.1.1.1 |
| 只读属性赋值 / 无 setter | TypeError | §10.1.9.2 |
| 原型非法 / 成环 | TypeError | §10.1.2.1 |
| Symbol 隐式转数值/字符串 | TypeError | §7.1.4 / §7.1.14 |
| ToPrimitive 失败 | TypeError | §7.1.1.1 |
| 迭代器结果非对象 / next 非函数 | TypeError | §7.4.3 |
| 标识符重复声明 | SyntaxError | §14.3.1 |
| const 赋值 | TypeError | §14.3.1 |
| 严格模式未声明赋值 | ReferenceError | §9.1.1.2.5 |
| 闭包变量未定义 | ReferenceError | §9.1.1.2.5 |
| `Object.keys(undefined)` 等 ToObject 失败 | TypeError | §7.1.18 |
| 定时器回调非函数 | TypeError | HTML Timers |

**注意**：`SymbolType::toString`（显式 `String(sym)`）合法，返回
`"Symbol(desc)"`；只有隐式 ToString/ToNumber（如 `sym + 1`）才抛 TypeError。

### 4. 未捕获异常渲染

`Task.cpp` 的 `exceptionMessage` 对 Error 对象调用其 `toString`
（`"Name: message"`，§20.5.3.4），因此未捕获错误现在打印
`Uncaught TypeError: Task entity is not a function` 而不是裸消息。

## 后果

- 优点：引擎内部错误与用户 `throw new TypeError(...)` 形态一致；带 stack 与
  `name`，可直接用于调试器/错误分类；后续 `try/catch` 与 `instanceof` 天然可用。
- 缺点：每次内部报错多分配一个 Error 对象（含 stack 字符串）。错误路径本就
  低频，可接受。
- 兼容性：`TypeKind::Exception` 判定不变，既有调用点无需改动；但读取
  `ExceptionData::child` 并当作 String 的代码需适配（仅 Task 一处）。
