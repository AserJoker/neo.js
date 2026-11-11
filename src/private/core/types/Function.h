// types/function.h - FunctionType + FunctionData
//
// 函数对象（ADR-021 / ADR-022）。本阶段仅 native 函数（script 路径留 VM 阶段）。
//
// FunctionData 继承 ObjectData（ADR-021 §1「类似对象的字段，继承对象模型」）：
//   函数也是对象——properties/order/symbols/prototype 直接复用对象模型，
//   属性查找/原型链/GC 遍历/clone 全部走 ObjectData 的实现。
//
// FunctionData 自有字段：
//   - closureScope：闭包作用域（**孤立**，parent=nullptr；Sema 收集的穿透闭包变量绑定于此。
//     生命周期随本 data：ref 归 0 时析构函数销毁它。调用期间临时挂到 rootScope 下，
//     调用结束摘回孤立——见 ADR-036）
//   - rootScope：创建时从作用域中获取的模块根作用域（借用，不拥有）
//   - activeCalls：当前对该函数的进行中调用数（重入计数）。闭包作用域的临时挂载
//     只在最外层调用进出时执行，避免内层调用提前摘除破坏外层查找链。
//   - native：native 函数指针（ADR-005 签名）
//   - minArgc：最少参数数量（调用时校验，不足返回 Exception）
//   - constructFn：可选构造钩子。非空时 construct 直接调它并用其对象返回值，
//     用于 call/construct 语义必须不同的内建（如 Boolean/Number/String 包装类：
//     call 返回原始值，new 返回包装对象）。
//   - constructible：是否可作为构造函数（ES2024 §7.2.4 IsConstructor）。
//     native 函数默认 true；箭头函数/方法等不可构造者由 setFunctionConstructible
//     显式置 false。绑定函数在创建时按 target 的可构造性继承（§10.4.1.3）。
//     注：本引擎中 Function 类型恒可调用（native 指针非空，绑定函数转发 target），
//     故不另设 callable 标志。
//
// 函数名不是 data 字段，而是函数对象上的 "name" 属性（writable=false /
// enumerable=false / configurable=true，ES2024 §20.2.4.2），由 createNativeFunction
// 初始化为 "<anonymous>"、setFunctionName 更新。
//
// Function 是孤立的：Sema 收集穿透的闭包变量，绑定到 closureScope，
// 运行时查找 closureScope → rootScope → global，不依赖定义时的中间词法链（ADR-013 §3）。
// closureScope 是 Scope*（非 Value*），按 ADR-021 §4 不由 GC 从函数出发标记；
// 其生命周期由本 data 的引用计数决定（ADR-036），不再挂在 root 作用域树下常驻。
//
// 本阶段 FunctionType 只实现 call 与属性/原型操作（委托 ObjectType）；
// 运算行为（add/sub/...）返回 Exception("not implement")。
//
// 关联 ADR: 005（native 等价）、013（闭包三层作用域）、018（对象模型）、
//          021（Function 结构）、022（调用）

#ifndef NEO_CORE_PRIVATE_TYPES_FUNCTION_H
#define NEO_CORE_PRIVATE_TYPES_FUNCTION_H

#include "core/IContext.h"  // NativeFn（public 层定义）
#include "core/IData.h"
#include "core/IType.h"
#include "core/SourceText.h"    // SourceSlice（ADR-047 §6）
#include "core/types/Object.h"  // FunctionData 继承 ObjectData
#include "infra/IAllocator.h"

#include <cstddef>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace neo::core {

class Scope;
class Value;

class FunctionType : public IType {
public:
    FunctionType() : IType(TypeKind::Function, true) {}
    const char* name() const override { return "function"; }

    // 本阶段仅实现 call；其余行为返回 Exception("not implement")。
    Value* add(IContext* ctx, Value* a, Value* b) override;
    Value* sub(IContext* ctx, Value* a, Value* b) override;
    Value* mul(IContext* ctx, Value* a, Value* b) override;
    Value* div(IContext* ctx, Value* a, Value* b) override;
    Value* toString(IContext* ctx, Value* self) override;

    // 调用：校验 minArgc 后调 native 指针。
    Value* call(IContext* ctx, Value* self, Value* thisArg,
                size_t argc, Value* argv[]) override;
    Value* construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) override;
    Value* getProperty(IContext* ctx, Value* self, Value* key) override;
    Value* setProperty(IContext* ctx, Value* self, Value* key, Value* value) override;
    bool hasProperty(IContext* ctx, Value* self, Value* key) override;
    Value* deleteProperty(IContext* ctx, Value* self, Value* key) override;
    Value* defineProperty(IContext* ctx, Value* self, Value* key,
                          Value* value, bool writable, bool enumerable,
                          bool configurable) override;
    Value* defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                          Value* setter, bool enumerable, bool configurable) override;
    Value* getPrototype(IContext* ctx, Value* self) override;
    Value* setPrototype(IContext* ctx, Value* self, Value* proto) override;
};

// 函数对象 data：继承 ObjectData 复用属性容器/原型链/GC 遍历/clone。
class FunctionData : public ObjectData {
public:
    Scope* closureScope;   // 闭包作用域（孤立，生命周期随本 data，ADR-036）
    Scope* rootScope;      // 模块根作用域（借用，不拥有）
    NativeFn native;       // native 函数指针
    size_t minArgc;        // 最少参数数量（调用时校验）
    NativeFn constructFn;  // 可选构造钩子（nullptr = native + 默认 this 语义）
    bool constructible = true;  // 是否可作为构造函数（IsConstructor，§7.2.4）
    // 是否为生成器函数（function*，ADR-045 §8）。为 true 时 call 不执行函数体，
    // 而是创建 Suspended 协程返回；生成器函数不可构造。
    bool generator = false;
    // 是否为异步生成器函数（async function*）。为 true 时 call 创建 AsyncGenerator。
    bool asyncGenerator = false;
    // 是否为 Environment 模式闭包（定义点处于 dynamic 作用域，或函数含直接 eval）：
    // closureScope 以 lookup-only 方式挂到定义点环境链，并沿链 pin（captureRef++）。
    // 调用期不再 reparent，析构时先销毁 closureScope 再自底向上解 pin。
    bool capturesEnvironment = false;
    // 函数体含直接 eval（Sema 标记）。创建时据此强制 Environment 模式。
    bool hasDirectEval = false;
    // class 构造器标记（ADR-047）：call 抛 TypeError，construct 走 class 分支。
    bool isClassConstructor = false;
    // [[HomeObject]]（class 方法/构造器/字段初始化器）：super 属性查找基准。
    Value* homeObject = nullptr;  // GC 需遍历
    // 函数源码切片（ADR-047 §6）：Function.prototype.toString 按需生成字符串。
    SourceSlice source;
    size_t activeCalls = 0;  // 进行中调用数（重入计数，闭包作用域挂载/摘除用）
    // bind 产生的绑定函数（ES2024 §10.4.1.3 BoundFunctionCreate）：
    // boundTarget 非空表示这是绑定函数，调用时以 boundThis 为 this、
    // 前置 boundArgs 再拼接调用实参。
    Value* boundTarget = nullptr;   // 被绑定的原函数
    Value* boundThis = nullptr;     // 绑定的 this
    std::vector<Value*> boundArgs;  // 前置实参（GC 需遍历）

    FunctionData(Scope* closure, Scope* root, NativeFn fn, size_t minArgs,
                 NativeFn ctor = nullptr)
        : closureScope(closure), rootScope(root), native(fn), minArgc(minArgs),
          constructFn(ctor) {}

    // 析构：销毁持有的孤立闭包作用域（其 values 随作用域一并清空）。
    // 本 data 由 GC sweep 在最后一个引用它的 Value 被回收时销毁（ADR-015/036）。
    ~FunctionData() override;

    // GC 遍历：对象字段 + bind 持有的原函数/this/前置实参
    void enumerateChildren(IGCVisitor* visitor) override;

    // clone：引用语义（ADR-019 §2）——ref++ 后返回 this，属性容器随之共享。
    // closureScope 随 data 生命周期（共享 data 即共享同一闭包作用域），rootScope 借用。
    IData* clone(neo::infra::IAllocator* alloc) const override {
        return ObjectData::clone(alloc);
    }
};

// class 实例字段定义（ADR-047 §4）。P1 只用 initializer；extraInitializers 供
// 装饰器 addInitializer 扩展，P3 启用。
struct FieldDef {
    Value* key = nullptr;                   // public key（P2 私有字段用 PrivateName）
    Value* initializer = nullptr;           // 初始化函数（nullptr = undefined）
    std::vector<Value*> extraInitializers;  // 装饰器追加的初始化回调
    bool isPrivate = false;
    bool isStatic = false;
};

// class 构造器专属 data（ADR-047）：派生 FunctionData，复用函数生态。
class ClassData : public FunctionData {
public:
    bool isDerived = false;              // [[ConstructorKind]]
    Value* superConstructor = nullptr;   // 父构造器（GC 遍历）
    std::vector<FieldDef> fields;        // 实例字段，按声明顺序
    std::vector<Value*> classInitializers;  // 装饰器 addInitializer（P3）

    ClassData(Scope* closure, Scope* root, NativeFn fn, size_t minArgs,
              NativeFn ctor = nullptr)
        : FunctionData(closure, root, fn, minArgs, ctor) {}

    void enumerateChildren(IGCVisitor* visitor) override;
};

}  // namespace neo::core

#endif  // NEO_CORE_PRIVATE_TYPES_FUNCTION_H
