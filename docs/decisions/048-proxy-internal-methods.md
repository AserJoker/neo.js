# ADR-048: Proxy 与对象内部方法层

- 状态：P2 已实现（核心 traps / 撤销 / 可调用适配 / 完整不变量 / 可扩展性）
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
| `[[IsExtensible]]` | `isExtensible(ctx, self)` | `ObjectData::extensible` |
| `[[PreventExtensions]]` | `preventExtensions(ctx, self)` | 置 `extensible=false` |

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
| `[[IsExtensible]]` | `isExtensible` | `(target)` |
| `[[PreventExtensions]]` | `preventExtensions` | `(target)` |

trap 缺失（undefined/null）时回退到 target 的对应内部方法。

## 4.1 不变量（P2）

所有不变量在 trap 返回后、结果交给调用方前校验，违反即 TypeError：

| 内部方法 | 不变量 |
| --- | --- |
| `getOwnPropertyDescriptor` | 不可配置属性不可隐藏；不可扩展 target 上不可隐藏/新增；新属性必须可配置；与 target 描述符兼容 |
| `ownKeys` | 不可扩展 target 的键集必须完全一致；可扩展 target 必须包含全部不可配置键；键不得重复 |
| `get` | 不可配置且不可写的数据属性，trap 值必须 SameValue |
| `set` | 同上，trap 报告成功时不得写入不同值 |
| `has` | trap 返回 false 时，不可隐藏不可配置属性，也不得对不可扩展 target 报告缺失 |
| `deleteProperty` | trap 返回 true 时不可删除不可配置属性 |
| `defineProperty` | 不可扩展 target 不可新增；已有属性必须兼容 |
| `getPrototypeOf` | 不可扩展 target 的原型不可改变 |
| `setPrototypeOf` | 同上 |
| `isExtensible` | trap 结果必须等于 target 实际可扩展性 |
| `preventExtensions` | trap 返回 true 时 target 必须已不可扩展 |

描述符转换与兼容性判断收敛到 `Object.cpp` 的
`propertyDescriptorFromValue` / `propertyDescriptorToValue` /
`isCompatiblePropertyDescriptor`，Proxy 与 `Object.defineProperty` /
`Object.getOwnPropertyDescriptor` 共用。`ObjectType::defineProperty` /
`defineAccessor` 与 `ordinarySet` 也执行不可扩展/兼容性校验，因此非 Proxy
对象同样遵守属性描述符规则。

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

- **P1**：内部方法层；Proxy 类型；11 个核心 trap；撤销；
  可调用/可构造适配；非对象 target/handler 校验；trap 结果基本校验。
- **P2（本 ADR）**：完整不变量（不可配置属性、不可扩展对象、ownKeys 完整性、
  getOwnPropertyDescriptor 一致性）；`ObjectData::extensible`；
  `isExtensible` / `preventExtensions` trap；`Object.getOwnPropertyDescriptor` /
  `Object.isExtensible` / `Object.preventExtensions`。
- **P3**：数组/字符串内建泛型化（`push` 等不再 `asArray`）；for-in / JSON；
  `class extends Proxy`、私有字段在 Proxy 上的语义；GC/撤销压力测试。

## 8. 已知边界（P2）

- `hasProperty` 返回 `Value*`（Boolean 或 Exception/Fatal），has trap
  抛错原样透传。
- 描述符转换已校验数据/访问器字段互斥与 get/set 可调用性。
- 不变量已覆盖 §10.5 全部 trap；`isExtensible` / `preventExtensions`
  不变量在 trap 返回后立即校验。
- 尚未实现：`Object.defineProperties` / `Object.getOwnPropertyDescriptors`；
  `Reflect` 命名空间；for-in 枚举顺序与 Proxy 的完整交互。
