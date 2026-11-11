# ADR-047: class 的引擎模型 — ClassData 派生 FunctionData + 函数环境 this/new.target

- 状态: 已接受 (2026-11-11)；P1（class 核心）、P2（私有字段/方法/访问器）、
  P3 的 native 扩展库、`extends null`、static block 引擎原语已实现；
  static block 的 Sema early error/VM 排序与装饰器延后
- 决策者: aserjoker（项目负责人） + CodeAgent
- 关联: ADR-013（三层作用域）、ADR-021（Function 结构）、ADR-022（函数调用）、
          ADR-032（对象/函数模型）、ADR-034（原型与 new）、ADR-036（闭包作用域孤立化）、
          ADR-044（GC 可达性）、ADR-045（协程）、ADR-046（Interrupt）

## 背景

规范把 class 描述为语法糖，但 class 有一组可观察语义，不能简单 desugar 成普通函数：

- class 构造器 `C()` 抛 `TypeError`，只能 `new`；
- `C.prototype` 不可写、不可配置；class 方法不可枚举、没有 `prototype` 属性、不可构造；
- class 体始终 strict；class 声明是 TDZ（let 式）；
- 派生类 `this` 在 `super()` 前未初始化；
- `super` 属性访问依赖 `[[HomeObject]]`，`super()` 依赖 `[[ConstructorKind]]`；
- `new.target`、私有字段、static block、字段初始化顺序；
- `Function.prototype.toString` 返回 class 源码；
- 派生类 `C.__proto__ === Super`、`C.prototype.__proto__ === Super.prototype`。

因此引擎层必须保留 class 语义。但 class 构造器本质上是**函数对象**：
`typeof C === "function"`、`C instanceof Function`、`C.bind(...)`、`Reflect.construct(C, ...)`
都成立。新增 `TypeKind::Class` 会让所有函数分派点都要同时接受 Class，漏接只会静默走错
分支；收益不足以抵消成本。

## 决策

### 1. TypeKind 保持 Function，ClassData 派生 FunctionData

```cpp
class FunctionData : public ObjectData {
    // 现有字段...
    bool isClassConstructor = false;  // [[FunctionKind]] = classConstructor
    Value* homeObject = nullptr;      // [[HomeObject]]，GC 遍历
    SourceSlice source;               // Function.prototype.toString 切片
};

class ClassData : public FunctionData {
public:
    bool isDerived = false;              // [[ConstructorKind]]
    Value* superConstructor = nullptr;   // GC
    std::vector<InstanceElement> instanceElements; // public/private 字段统一有序
    std::vector<Value*> classInitializers; // 装饰器 addInitializer（P3）
};
```

- class 构造器：`ClassData`，`isClassConstructor=true`、`constructible=true`；
- class 方法：普通 `FunctionData`，`constructible=false`、`homeObject=prototype`、
  创建时 `hasPrototypeProperty=false`；
- 普通函数完全不变；
- `FunctionType::call` 对 `isClassConstructor` 抛错；
- `FunctionType::construct` 对 class 走独立构造算法；
- `typeof`/`instanceof`/`bind`/`Reflect.construct`/`Object.prototype.toString`
  复用现有 Function 逻辑，不需要新增分派。

`hasPrototypeProperty` 是**创建选项**，不是后置字段：`prototype` 属性一旦按普通函数
创建就是 non-configurable，事后无法删除。class 方法必须在创建时就不带 `prototype`。

### 2. this 与 new.target 放 ArgumentScope，Context 缓存当前函数环境

ArgumentScope 是**函数环境记录**，不是普通 block scope。它增加专用槽位：

```cpp
class Scope {
    Value* thisValue_ = nullptr;
    bool hasOwnThis_ = false;
    Value* newTarget_ = nullptr;
    bool hasOwnNewTarget_ = false;
public:
    bool hasOwnThis() const;
    Value* thisValue() const;
    void setThis(Value*);
    bool hasOwnNewTarget() const;
    Value* newTarget() const;
    void setNewTarget(Value*);
};
```

这些槽位**不参与**普通 `lookup(name)`，避免被 with 对象属性、`var this`、`let this`
等路径干扰。VM 用专用 opcode `load_this` / `load_new_target` 访问。

Context 增加缓存：

```cpp
Scope* currentFunctionEnv_;
```

`invokeNativeFunction` 像保存 root/current 一样保存/恢复 `currentFunctionEnv_`：

- 普通调用：新 ArgumentScope 的 this = thisArg，new.target = undefined；
- 构造调用：新 ArgumentScope 的 this = 新建对象或 Uninitialized，new.target = 传入值；
- 返回/Fatal：恢复调用方的 `currentFunctionEnv_`；
- 构造函数非 async/generator，不需要把构造现场挂到协程堆上。

#### this 的语义

- 基类构造器：引擎创建 this 后绑定；VM 在构造体开头初始化字段；
- 派生类构造器：this 先绑定 `Uninitialized` 哨兵；`super()` 成功后**原地 replaceMeta**
  为实例对象，之前创建的箭头函数捕获的是同一个 `Value*`，自动看到初始化后的 this；
- `load_this` 看到 Uninitialized 抛 ReferenceError；
- 箭头函数不创建自己的 this 槽，创建时把外层 this 的 `Value*` 记录到 FunctionData
  （共享槽位，不是快照），调用时以它为 thisArg；
- 全局/模块顶层：模块 this = undefined，脚本 this = globalThis，由 VM 在入口绑定；
- native 函数直接使用 thisArg，不需要 ArgumentScope 的 this 槽；但 native 内执行 eval
  时仍能通过 currentFunctionEnv_ 提供调用方 this；
- `with` 不拦截 this：`load_this` 绕过 WithScope 的对象属性查找。

#### new.target 的语义

- 普通调用/方法调用：undefined；
- `new C()`：C；
- `Reflect.construct(C, args, D)`：D；
- `super()`：把当前 new.target 传给父构造器；
- 箭头函数：词法继承外层函数的 new.target。建议在箭头创建时把外层 new.target
  快照捕获到闭包（new.target 在一次调用内不变，快照正确且简单）；
- 绑定函数：若 `newTarget == boundFn`，按规范替换为 target，再传给 target；
- `load_new_target` 读取 `currentFunctionEnv_->newTarget()`；箭头走捕获的快照。

### 3. 构造算法

`FunctionType::construct` 对 class 走独立分支：

```
construct(C, newTarget, args):
  if C.isClassConstructor:
     if C.isDerived:
         thisValue = UNINITIALIZED
     else:
         thisValue = OrdinaryCreateFromConstructor(newTarget)
         // 原型取 newTarget.prototype，不是 C.prototype
     result = invoke(C, thisValue, args, newTarget)
     if IsObject(result): return result
     if result is undefined:
         if thisValue is UNINITIALIZED: throw ReferenceError
         return thisValue
     if C.isDerived: throw TypeError   // 派生类返回原始值
     return thisValue                  // 基类忽略原始值
  // 普通函数保持现有逻辑
```

`constructSuper`：

```
constructSuper(superCtor, newTarget, args):
  if superCtor is null: throw TypeError
  if thisValue is not UNINITIALIZED: throw ReferenceError  // super() 两次
  obj = construct(superCtor, newTarget, args)
  thisValue = obj
  return obj
```

`FunctionType::call` 首行：

```cpp
if (d->isClassConstructor) {
    return typeError("Class constructor cannot be invoked without 'new'");
}
```

### 4. 字段初始化与装饰器

`initializeInstanceElements(instance, classCtor)` 按 ClassData 顺序执行：

```cpp
struct InstanceElement {
    enum class Kind { PublicField, PrivateField };
    Kind kind = Kind::PublicField;
    Value* key = nullptr;                  // PublicField 的属性键
    PrivateName* name = nullptr;           // PrivateField 的私有名
    Value* initializer = nullptr;          // 基础初始化函数（可被装饰器包装）
    std::vector<Value*> extraInitializers; // 装饰器 addInitializer
};
```

执行顺序：

```
1. 给所有私有方法/访问器加 brand（与字段声明顺序无关）
2. for each elem in ClassData.instanceElements（源码声明顺序）:
     value = elem.initializer ? call(elem.initializer, this) : undefined
     if PublicField:
         defineProperty(this, elem.key, value, writable=true, enumerable=true, configurable=true)
         for each extra in elem.extraInitializers: call(extra, this)
     else:
         this.[[PrivateElements]][elem.name] = value
3. for each classInitializer in ClassData.classInitializers: call(classInitializer, this)
```

public/private 字段共享同一有序列表，因此 `a; #b; c` 严格按 `a、b、c` 初始化。

时机由 VM 控制：

- 基类：构造体开头调用 `initializeInstanceElements(this, C)`；
- 派生类：`super()` 返回后调用 `initializeInstanceElements(this, C)`。

装饰器不下沉到引擎。VM 在类定义时应用装饰器：

- 字段装饰器：包装 initializer 或追加 `extraInitializers`；
- 方法装饰器：把 `addInitializer` 追加到 `classInitializers`；
- 类装饰器：可以替换构造器，VM 负责把 ClassData 转移到新构造器或包装；
- 装饰器元数据：VM 侧表或 symbol 属性，引擎不感知。

`InstanceElement` 从一开始就用 vector 形式，P1 只放 public initializer，P2 合并
private 字段，P3 加装饰器不需要改 `initializeInstanceElements` 签名。

### 5. 私有字段（P2）

PrivateName 是引擎内部对象，不是 JS 可见的 string/symbol：

- 身份是 `PrivateName*` 指针，每次 class 求值唯一；`description` 只用于错误消息；
- 生命周期用**引用计数**（与 SourceText 同一模式）：ClassData 的定义表和实例
  `PrivateSlots` 各持一份，最后一个引用释放时 delete。PrivateName 不持有任何
  Value，天然无环，不需要 GC 标记；
- 实例通过 `PrivateSlots`（`vector<PrivateSlot>`）持有 brand 和字段值：
  `PrivateSlot{PrivateName* name; Value* value;}`，`value == nullptr` 表示只有
  brand（私有方法/访问器），非空表示字段值；
- 反射不可见：`Object.keys`/`getOwnPropertyNames`/`for...in` 都看不到私有字段。

槽位载体：**所有对象负载都直接持有 `PrivateSlots`**。

```
ObjectData（内联 PrivateSlots slots）
├── FunctionData → ClassData（静态私有槽即构造器对象自身的 slots）
├── ArrayData / ErrorData / PrimitiveWrapperData / PromiseData
├── CoroutineData / AsyncGeneratorData / ModuleNamespaceData / IteratorData
```

- 规范里任何对象都有 `[[PrivateElements]]`；派生类 `super()` 可以返回任意对象
  （对象字面量、函数、内建 exotic），随后在其上初始化派生类私有字段。
  因此不能只让部分 Data 携带槽位；
- `ObjectData` 直接内联 `slots`，析构归还 PrivateName 引用，
  `enumerateChildren` 标记字段值；子类只需链式调用 `ObjectData::enumerateChildren`；
- `ClassData` 的静态私有字段/brand 直接存入继承来的 `slots`，不再单独维护
  `staticSlots`；
- 代价：每个对象多一个空 vector（24 字节，空时无堆分配）。当前以正确性优先，
  后续用自研轻量容器（内联小缓冲/紧凑布局）统一优化。

**初始化顺序**：

- public/private 实例字段共享 `ClassData::instanceElements` 有序列表，VM 按源码
  顺序调用 `addInstanceField`/`addPrivateField`；
  `initializeInstanceElements` 先加私有方法/访问器 brand，再按该列表逐个初始化，
  最后执行装饰器 `classInitializers`；
- 静态私有方法/访问器在 `addStaticPrivateMethod/Accessor` 时即挂 brand；
  静态私有字段由 VM 在声明位置调用 `initializeStaticPrivateField`，
  从而与 static block / public static field 保持源码顺序；
  `initializeStaticPrivateElements` 仅作批量便捷入口。

VM 职责不变：私有名的词法解析、`#x in obj`/私有访问的字节码降级、嵌套 class 的
PrivateEnvironment 链。引擎只提供 `createPrivateName`、`addPrivate*`、
`initializeInstanceElements`/`initializeStaticPrivateField`/
`initializeStaticPrivateElements`、`getPrivate`/`setPrivate`/`privateIn`/
`hasPrivate`、`getPrivateMethod`/`getPrivateGetter`/`getPrivateSetter`。

### 6. sourceText：全局单例 + 函数切片

```cpp
struct SourceText {
    std::string text;   // 不可变
    uint32_t refCount;  // 或 GC 托管
};

struct SourceSlice {
    SourceText* source = nullptr;
    uint32_t offset = 0;
    uint32_t length = 0;
};
```

- module/eval 各自持有 SourceText；
- 函数只存 SourceSlice，不复制字符串；
- 函数 slice 对 SourceText 持引用，保证函数逃逸后源码仍可用；
- `Function.prototype.toString` 在调用时按 slice 创建 JS string；
- native 函数 slice 为空 → `[native code]`；
- release 模式可裁剪 source，slice 为空 → fallback；
- debug info（line/column）引用同一个 SourceText + offset；
- class/method/field initializer 各自存自己的 slice。

### 7. 引擎原语清单（P1）

| 原语 | 作用 |
|---|---|
| `FunctionCreateOptions` / `createFunction` | 创建时决定 prototype/constructible/strict/class 标记 |
| `setFunctionClassConstructor(fn, isDerived, superCtor)` | 标记 class 构造器 |
| `setFunctionHomeObject(fn, homeObject)` | 设置 `[[HomeObject]]` |
| `setFunctionSource(fn, slice)` | `Function.prototype.toString` |
| `construct(fn, newTarget, argc, argv)` | 带 new.target 的构造入口 |
| `constructSuper(superCtor, newTarget, argc, argv)` | 执行 `super()` |
| `initializeThis(value)` / `getThis()` / `isThisInitialized()` | 派生类 this 状态 |
| `getSuperProperty(homeObject, key, receiver)` | `super.prop` 读 |
| `setSuperProperty(homeObject, key, value, receiver)` | `super.prop` 写 |
| `initializeInstanceElements(instance, classCtor)` | brand + public/private 字段按声明序初始化 |
| `addInstanceField(classCtor, key, initializer)` | 注册 public 字段定义 |
| `addPrivateField/addPrivateMethod/addPrivateAccessor` | 注册私有元素（P2） |
| `initializeStaticPrivateField(classCtor, name)` | 按声明位置初始化静态私有字段 |
| `privateIn(obj, name)` / `getPrivate/setPrivate` | `#x in obj` 与私有字段读写 |
| `currentFunction()` | 当前正在执行的函数 Value（native 扩展库） |
| `registerClass(NativeClassDef)` | 一次性创建 native class |
| `classSuperConstructor/classPrivateName/functionHomeObject` | native class 元数据查询 |
| `runStaticBlock(classCtor, body)` | 创建并执行 native static block（this=C） |
| `newTarget()` | 当前调用的 new.target |

复用现有原语：`defineProperty`、`setPrototype`、`createObject`、`setFunctionName`、
`setFunctionStrict`、`setFunctionConstructible`。

### 8. VM / Sema 职责

引擎不做：

- class 语法解析、class body 求值顺序；
- class 声明的 TDZ、提升、绑定；
- `extends` 表达式求值；
- 创建 C / prototype 并链接（用引擎原语组合）；
- 创建方法/字段初始化函数/static block 函数，调用引擎 setter；
- `super` / `new.target` / private 的字节码降级；
- 字段初始化时机（基类开头、派生类 super 后）；
- static 字段求值、static block 执行（this = C）；
- `class extends null` 的 proto 组合；
- `name` / `length` 推断；
- class / method 源码采集与传入；
- 装饰器应用。

### 9. native 扩展库

native 侧注册 class 需要三件事：取回当前函数、一次性构建 class、查询 class 元数据。

**currentFunction**：

- `Context` 增加 `currentFunction_`，`invokeNativeFunction` 在所有返回路径
  （正常/Fatal/挂起/OOM）保存/恢复；
- native 构造体据此取回自身 `FunctionData`：
  `initializeInstanceElements(self, ctx->currentFunction())`；
- `FunctionData` 增加 `ownerClass`（GC 遍历），方法/字段初始化器创建时指向所属
  class 构造器，`classPrivateName/classSuperConstructor` 可接受成员函数入口。

**registerClass**：

```cpp
struct NativeClassDef {
    std::string_view name;
    NativeFn constructor;  // base 可为空（默认构造体只初始化字段）
    size_t length;
    Value* superCtor;      // nullptr = base；Null Value = extends null
    std::vector<NativeClassMethod> methods, staticMethods;
    std::vector<NativeClassField> fields, staticFields;
    std::vector<NativeClassPrivateField> privateFields, staticPrivateFields;
    std::vector<NativeClassPrivateMethod> privateMethods, staticPrivateMethods;
    std::vector<NativeClassPrivateAccessor> privateAccessors, staticPrivateAccessors;
};
Value* registerClass(const NativeClassDef&);
```

- 创建构造器（`isClassConstructor/isDerived/superConstructor/homeObject`）；
- 定义 `prototype`（不可写/不可配置）、`constructor`、方法（不可枚举、不可构造、
  无 `prototype`、带 homeObject/ownerClass）；
- 注册 public/private 字段与静态字段；静态私有字段按声明顺序立即初始化；
- 私有方法/访问器注册 brand；
- `class extends null`：`prototype.__proto__ = null`、`C.__proto__ = Function.prototype`，
  派生构造体不调用 `super()` 时必须显式返回对象。

**元数据查询**：

- `classSuperConstructor(classCtorOrMember)` → 父构造器；
- `classPrivateName(classCtorOrMember, description)` → 私有名（借用引用）；
- `functionHomeObject(fn)` → `[[HomeObject]]`。

**模块注入**：

- `setModuleLoader(id, loader)` + `importModule(id)`：loader 在模块 root scope 执行；
- loader 内 `registerClass` → `define(name, cls)` → `exportBinding(exports, name, name)`；
- 脚本侧 `import` 取回构造器后即可 `new`（前端/VM 实现后）。

### 10. static block

static block 在规范里就是一个函数对象：`ClassStaticBlockDefinitionEvaluation`
创建函数，`[[HomeObject]] = F`，再以 `this = F` 调用。引擎按同样方式实现：

```cpp
Value* runStaticBlock(Value* classCtor, NativeFn body);
```

- 固定选项：`strict`、`constructible=false`、`hasPrototypeProperty=false`、
  `homeObject=ownerClass=classCtor`；
- 以 `call(fn, classCtor, 0, nullptr)` 执行：`this = C`、`new.target = undefined`；
- `var`/函数声明天然只属于块（函数体作用域）；
- 嵌套箭头按词法继承 `this`/`super`；
- 私有名通过 `classPrivateName(currentFunction(), "x")` 解析；
- 脚本 static block 由 VM 用同一组 `FunctionCreateOptions` 创建脚本函数，
  在静态元素声明位置调用。

Sema 负责的 early error（函数封装不会自动禁止）：

- 块顶层 `arguments`、`return`、`await`、`yield`、`super()`；
- `break`/`continue` 跳到块外标签；
- `this = ...`（strict 下非法）。

顺序：static block 按静态元素声明顺序执行。VM 在每个块的声明位置调用；
静态私有字段用 `initializeStaticPrivateField` 在该位置初始化，静态私有方法/
访问器的 brand 在 `add*` 时已挂上。引擎的 `runStaticBlock` 只负责“执行”，
不负责排序。

## 分阶段

- **P1：class 核心** — ClassData、isClassConstructor、homeObject、source slice、
  构造算法、this/new.target 函数环境、super()/super.prop、方法、public 实例字段、
  static 方法/字段、strict、name/length、toString、prototype 描述符。
- **P2：私有字段** — PrivateName、PrivateSlots、brand、private field/method/accessor、
  `#x in obj`。
- **P3：边角** — native 扩展库（currentFunction/registerClass/元数据查询）、
  `class extends null`、绑定函数/Reflect.construct 的 newTarget 细节；
  static block、装饰器、源码文本保真延后。
- **P4：静态化/JIT** — ClassData 增加字段偏移/vtable/布局元数据，引擎只暴露查询。

## P1 实现状态

已落地：

- `Scope` 增加 `thisValue_/hasOwnThis_`、`newTarget_/hasOwnNewTarget_` 专用槽；
  `Context` 增加 `currentFunctionEnv_`，invoke 在所有返回路径保存/恢复。
- `FunctionData` 增加 `isClassConstructor/homeObject/SourceSlice`；新增
  `ClassData : FunctionData`（isDerived/superConstructor/instanceElements/classInitializers）。
- `FunctionCreateOptions` 支持 `constructible/hasPrototypeProperty/strict/
  isClassConstructor/isDerived/homeObject/superConstructor/source`。
- `SourceText` 引用计数 + `SourceSlice`；`Function.prototype.toString` 按切片输出。
- `Context::construct(fn,newTarget,...)`、`constructSuper`、`getThis`、
  `initializeThis`、`isThisInitialized`、`newTarget`、`getSuperProperty`、
  `setSuperProperty`、`createUninitialized`。
- `addInstanceField`、`addClassInitializer`、`initializeInstanceElements`；
  `setFunctionClassConstructor` 仅更新已由创建选项分配 ClassData 的构造器。
- 绑定函数构造：`newTarget == boundFn` 时替换为 target（ES2024 §10.4.1.3）。
- 构造返回对象归属调用方作用域：派生类 this 由内层 execScope 的 super() 创建，
  外层 construct 返回时显式 `current()->add(r)`，避免 execScope pop 后被 GC 回收。

实现偏差/边界：

- `getSuperProperty` 在 homeObject 原型为 null/undefined 时抛 TypeError（按
  `Reflect.get(null, ...)` 语义）。
- `setSuperProperty` 从 base 链找描述符：访问器以 receiver 为 this 调 setter；
  数据属性可写时在 receiver 上定义自有属性。
- `this`/`new.target` 专用槽不进入 `namedValues_`，`load_this/load_new_target`
  由 VM 专用 opcode 访问。

## P2 实现状态

已落地：

- `PrivateName`（`src/public/core/PrivateName.h`）：description + 引用计数，
  身份按指针比较；`create/retain/release` 为宿主堆操作，不进 GC。
- `PrivateSlot`/`PrivateSlots` 直接内联在 `ObjectData`；析构时归还 PrivateName
  引用，`enumerateChildren` 标记字段值。所有对象负载（含函数与内建 exotic）
  因此天然可承载私有槽。
- `ClassData` 增加 `instanceElements`（public/private 字段统一有序列表）、
  `staticPrivateFields/privateElements/staticPrivateElements`；静态私有字段/brand
  存在构造器对象自身的 `slots` 中。
- 引擎原语：`createPrivateName/retainPrivateName/releasePrivateName`、
  `addInstanceField/addPrivateField/addStaticPrivateField`、
  `addPrivateMethod/addPrivateAccessor`、`addStaticPrivateMethod/
  addStaticPrivateAccessor`、`initializeInstanceElements`、
  `initializeStaticPrivateField/initializeStaticPrivateElements`、
  `getPrivate/setPrivate/privateIn/hasPrivate`、`getPrivateMethod/
  getPrivateGetter/getPrivateSetter`。

实现边界：

- `initializeInstanceElements` 先给私有方法/访问器加 brand，再按
  `instanceElements` 的源码声明顺序交错初始化 public/private 字段
  （ES2024 §10.2.1.2），最后执行装饰器回调。
- 静态私有方法/访问器在 add* 时即挂 brand；静态私有字段由
  `initializeStaticPrivateField` 在声明位置初始化，可与 static block 交错。
- `getPrivate`/`setPrivate` 只处理字段；brand-only 槽（方法/访问器）返回 TypeError，
  方法/访问器必须走 `getPrivateMethod/Getter/Setter`。
- 私有方法/访问器读取做种类校验：方法位置读到访问器、访问器位置读到方法、
  accessor 缺 getter/setter 均返回 TypeError（PrivateGet/PrivateSet 语义）。
- `privateIn` 对非对象 RHS 返回 TypeError；`hasPrivate` 仅作引擎内部 brand 判定。
- 静态私有访问以 `obj == classCtor` 区分，从 `staticPrivateElements` 取函数。
- 任意对象（对象字面量、函数、内建 exotic）都可承载私有槽，覆盖
  `super()` 返回任意对象的规范语义。

## P3 实现状态（native 扩展库 + extends null + static block）

已落地：

- `Context::currentFunction_`：invoke 在所有返回路径保存/恢复；
  `FunctionData::ownerClass`（GC 遍历）标记方法/字段初始化器所属 class。
- `NativeClassDef` + `Context::registerClass`：构造器/prototype/方法/字段/
  私有元素/静态元素一次创建；base 类可省略构造体（默认只初始化字段）。
- 元数据查询：`classSuperConstructor`、`classPrivateName`、`functionHomeObject`，
  均支持传 class 构造器或其成员函数。
- `class extends null`：`prototype.__proto__ = null`、`C.__proto__ =
  Function.prototype`；派生构造体未调用 `super()` 时须显式返回对象。
- 模块注入：loader 内 `registerClass` + `define` + `exportBinding`，
  `importModule` 取回构造器后可 `new`。
- `runStaticBlock`：以 strict/不可构造/无 prototype/homeObject=ownerClass=C
  创建函数并以 `this=C` 调用；测试覆盖 this/super/私有名/顺序/错误透传。
- `NativeClassTest` 覆盖 currentFunction、base/derived 注册、私有/静态私有、
  `extends null`、模块 loader 导出；`StaticBlockTest` 覆盖 static block 契约。

延后：

- static block 的 Sema early error（arguments/return/await/yield/super()/
  越界 break）与 VM 按声明位置调用；
- 装饰器应用（`extraInitializers`/`classInitializers` 已预留）；
- 源码文本保真。

## 后果

- 优点：class 语义完整保留；复用 Function 的全部生态，避免新增 TypeKind 的全量分派审计；
  this/new.target 与作用域生命周期一致，eval/with/箭头函数语义自然；字段初始化与装饰器
  通过有序初始化步骤扩展，不需要改引擎签名。
- 缺点：ArgumentScope 增加两个专用槽；Context 需要维护 currentFunctionEnv_ 并在所有
  调用路径保存/恢复；ClassData 与 FunctionData 的 GC 遍历需要同步扩展；sourceText 需要
  引用计数或 GC 托管。
- 风险：`super()` 在箭头函数中的语义、绑定函数/Reflect.construct 的 newTarget 传播、
  类装饰器替换构造器后的 ClassData 归属、static block 与静态字段的交错执行，
  需在对应阶段单独验证。
