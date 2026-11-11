# ADR-048: Proxy 与对象内部方法层

- 状态：P1 已实现（核心 traps / 撤销 / 可调用适配）
- 日期：2026-11-11
- 关联：ADR-016（Type 体系）、ADR-018（对象模型）、ADR-047（class）

## 1. 问题

Proxy 不是"带特殊属性的普通对象"：它的所有对象内部方法（`[[Get]]`、
`[[Set]]`、`[[HasProperty]]`、`[[OwnPropertyKeys]]` …）都转发到 handler 上的
trap。若属性算法继续直接读取 `ObjectData::properties/prototype`，那么：

- 原型链上的 Proxy 会被绕过（`obj.x` 直接读 prototype 的容器）；
- `Object.keys` / `hasOwnProperty` / `in` / `instanceof` 等内建各自直接
  `static_cast<ObjectData*>`，无法看到 trap；
- 可调用/可构造代理会被 `TypeKind::Function` 的硬判定拒绝。

因此 Proxy 的前置条件是：把对象属性算法收敛到**逐跳虚分派的内部方法层**。

## 2. 内部方法层

`IType` 新增以下虚方法（默认实现为普通对象算法，位于
`src/private/core/types/Object.cpp`）：

| 内部方法 | 签名 | 默认算法 |
| --- | --- | --- |
| `[[GetOwnProperty]]` | `getOwnProperty(ctx, self, key, exists, out)` | OrdinaryGetOwnProperty |
| `[[OwnPropertyKeys]]` | `ownKeys(ctx, self, out)` | OrdinaryOwnPropertyKeys |
| `[[Get]]` | `getWithReceiver(ctx, self, key, receiver)` | OrdinaryGet |
| `[[Set]]` | `setWithReceiver(ctx, self, key, value, receiver, ok)` | OrdinarySet |

约定：

- 成功返回 `nullptr`；trap 抛错返回 `Exception/Fatal`；
- `ok=false` 表示 `[[Set]]` 返回 false，由调用方按严格模式决定是否抛
  TypeError；
- 原型链遍历必须调用父对象的 `getWithReceiver` / `setWithReceiver` /
  `hasProperty`，不能只查 `getOwnProperty`——否则 Proxy 的 get/set/has trap
  不会触发；
- `Property` / `PropertyKey` 上移到公共 `IType.h`，供跨层传递描述符与规范键。

`ownPropertyKeys` 自由函数改为返回错误通道（`Value*` + out vector），
`Object.keys/values/entries/assign/getOwnPropertyNames` 与
`Context::ownPropertyKeys` 均透传 trap 错误。

索引型 exotic（Array / String 包装）继续通过
`hasIndexedElements/hasIndexedLength/indexedLength/indexedElement` 参与默认
算法；新增 `hasIndexedLength(self)` 区分"有索引元素"与"有 length 属性"
（Boolean/Number/Symbol 包装没有 length）。

## 3. Proxy 数据模型

```cpp
class ProxyData : public ObjectData {
    Value* target;   // [[ProxyTarget]]
    Value* handler;  // [[ProxyHandler]]；nullptr = 已撤销
};

class ProxyType : public ObjectType { ... };
```

- 复用 `ObjectData` 只为对象布局判定/GC/私有槽基础设施；Proxy 自身的
  `properties/order/symbols/prototype` 不使用。
- `TypeKind::Proxy` 是对象类型（`objectLayout=true`）。
- `createProxy(target, handler)` 校验两者均为对象（ES2024 §10.5.1）。
- 撤销只把 `handler` 置 nullptr；此后任一内部方法在 `lookupTrap` 处抛
  TypeError。

## 4. Trap 分派

| 内部方法 | trap | 参数 |
| --- | --- | --- |
| `[[Get]]` | `get` | `(target, key, receiver)` |
| `[[Set]]` | `set` | `(target, key, value, receiver)` |
| `[[HasProperty]]` | `has` | `(target, key)` |
| `[[Delete]]` | `deleteProperty` | `(target, key)` |
| `[[OwnPropertyKeys]]` | `ownKeys` | `(target)` |
| `[[GetOwnProperty]]` | `getOwnPropertyDescriptor` | `(target, key)` |
| `[[DefineOwnProperty]]` | `defineProperty` | `(target, key, descriptor)` |
| `[[GetPrototypeOf]]` | `getPrototypeOf` | `(target)` |
| `[[SetPrototypeOf]]` | `setPrototypeOf` | `(target, proto)` |
| `[[Call]]` | `apply` | `(target, thisArg, argArray)` |
| `[[Construct]]` | `construct` | `(target, argArray, newTarget)` |

trap 缺失（undefined/null）时回退到 target 的对应内部方法。
`isExtensible` / `preventExtensions` 属 P2（引擎当前没有可扩展性状态）。

## 5. 可调用 / 可构造适配

- `detail::isCallable` / `isConstructor` 对 Proxy 递归检查 target；
- `Function.prototype.call/apply/bind`、`Object.prototype.toLocaleString`、
  `instanceof` / `OrdinaryHasInstance` 改用 `isCallable`，不再硬判
  `TypeKind::Function`；
- `Array.isArray` 递归穿透 Proxy target（ES2024 §7.2.2）；
- `Object.prototype.toString` 对可调用 Proxy 输出 `[object Function]`；
- `Function.prototype.toString` 对 Proxy 抛 TypeError；
- 绑定函数按 `isConstructor(target)` 继承可构造性。

## 6. 内建

- `Proxy(target, handler)`：必须 `new`；无 `prototype` 属性；
- `Proxy.revocable(target, handler)`：返回 `{ proxy, revoke }`；
  revoke 通过 `[[HomeObject]]` 捕获 proxy（内部实现细节，GC 由
  `FunctionData::enumerateChildren` 遍历）。

## 7. 分阶段

- **P1（本 ADR）**：内部方法层；Proxy 类型；11 个核心 trap；撤销；
  可调用/可构造适配；非对象 target/handler 校验；trap 结果基本校验。
- **P2**：完整不变量（不可配置属性、不可扩展对象、ownKeys 完整性、
  getOwnPropertyDescriptor 一致性）；`ObjectData::extensible`；
  `isExtensible` / `preventExtensions` trap；`Object.getOwnPropertyDescriptor`。
- **P3**：数组/字符串内建泛型化（`push` 等不再 `asArray`）；for-in / JSON；
  `class extends Proxy`、私有字段在 Proxy 上的语义；GC/撤销压力测试。

## 8. 已知边界（P1）

- `hasProperty` 已改为返回 `Value*`（Boolean 或 Exception/Fatal），has trap
  抛错原样透传。
- `getOwnPropertyDescriptor` 描述符只做基本转换，未校验数据/访问器字段
  互斥。
- 不变量尚未实现，因此"trap 撒谎"（如对不可配置属性返回 undefined）
  不会报错。
