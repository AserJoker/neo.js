// Function.cpp - FunctionType 行为方法实现
//
// 本阶段仅实现 call（native 调用）；其余行为返回 Exception("not implement")，
// 待未来包装类（对象/属性）阶段补齐。
//
// call 流程：
//   1. argc < minArgc → Exception("<minArgc> arguments required, but only <argc> present")
//   2. 调 native(ctx, thisArg, argc, argv)，返回值直接透传（含 Exception/Fatal）
//
// 关联 ADR: 005（native 等价）、021（Function 结构）、022（调用）

#include "core/FunctionInvoke.h"
#include "core/IContext.h"
#include "core/Scope.h"
#include "core/Value.h"
#include "core/IGCEngine.h"
#include "core/types/Function.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <vector>

namespace neo::core {

// —— 运算：走 ToPrimitive（ES2024 §13.15.3 / §13.15.4）——
// 函数对象默认 ToPrimitive 得源码字符串（Function.prototype.toString），
// 故 + 为字符串拼接、算术为 NaN。

Value* FunctionType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::addValues(ctx, a, b);
}
Value* FunctionType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '-', a, b);
}
Value* FunctionType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '*', a, b);
}
Value* FunctionType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = detail::passthrough(a, b)) return p;
    return detail::numericOp(ctx, '/', a, b);
}
Value* FunctionType::toString(IContext* ctx, Value* self) {
    // ToString(function) = ToPrimitive(STRING) 后递归（ES2024 §7.1.14）
    return detail::toStringValue(ctx, self);
}

namespace {

// 最少参数校验：不足返回 Exception，否则返回 nullptr。
Value* checkMinArgc(IContext* ctx, const FunctionData* d, size_t argc) {
    if (argc >= d->minArgc) return nullptr;
    std::string msg = std::to_string(d->minArgc) + " arguments required, but only " +
                      std::to_string(argc) + " present";
    return detail::typeError(ctx, msg);
}

}  // namespace

Value* FunctionType::call(IContext* ctx, Value* self, Value* thisArg,
                          size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());

    // 绑定函数（bind 产物）：以 boundThis 为 this，前置 boundArgs 再拼接实参。
    // ES2024 §10.4.1.3 [[Call]]。
    if (d->boundTarget != nullptr) {
        std::vector<Value*> args;
        args.reserve(d->boundArgs.size() + argc);
        args.insert(args.end(), d->boundArgs.begin(), d->boundArgs.end());
        if (argc > 0) args.insert(args.end(), argv, argv + argc);
        return ctx->call(d->boundTarget, d->boundThis,
                         static_cast<size_t>(args.size()), args.data());
    }

    if (Value* err = checkMinArgc(ctx, d, argc)) return err;
    return invokeNativeFunction(ctx, self, thisArg, argc, argv);
}

// 析构：销毁孤立闭包作用域（ADR-036）。其 values 随作用域一并清空，
// 这些 Value 若已不可达，由下一轮 GC 回收（本轮标记阶段它们仍是根，不会误回收）。
// 若 Fatal 路径遗留了挂载状态，~Scope 会先把自己从 rootScope 摘除。
FunctionData::~FunctionData() {
    if (closureScope != nullptr) {
        closureScope->destroySelf();
        closureScope = nullptr;
    }
}

void FunctionData::enumerateChildren(IGCVisitor* visitor) {
    ObjectData::enumerateChildren(visitor);
    if (boundTarget != nullptr) visitor->visit(boundTarget);
    if (boundThis != nullptr) visitor->visit(boundThis);
    for (Value* v : boundArgs) {
        if (v != nullptr) visitor->visit(v);
    }
    // 孤立闭包作用域不在 global 作用域树上，只能由持有它的函数对象主动标记
    // （ADR-044）。函数对象不可达时本方法不会被调用，闭包作用域及其捕获值
    // 自然不被标记——「函数 → closureScope → 捕获值 → 函数」的环因此可回收。
    if (closureScope != nullptr) visitor->visitScope(closureScope);
}

// 构造调用（new，ADR-034）：
//   1. 新建 this 对象，原型 = fn.prototype（非 Object/null 时按 null 处理）
//   2. 以 this 为 thisArg 调 native（new 强制新对象，忽略 bind 的 self）
//   3. native 返回 Object/Function 则用它，否则用 this；Exception/Fatal 直接透传
Value* FunctionType::construct(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    auto* d = static_cast<FunctionData*>(self->data());

    // 不可构造（IsConstructor == false）→ TypeError（§7.2.4）
    if (!d->constructible) {
        return detail::typeError(ctx, "function is not a constructor");
    }

    // 绑定函数（bind 产物）：转发给 [[BoundTargetFunction]]，忽略 boundThis，
    // 前置 boundArgs（ES2024 §10.4.1.3 [[Construct]]）。
    if (d->boundTarget != nullptr) {
        std::vector<Value*> args;
        args.reserve(d->boundArgs.size() + argc);
        args.insert(args.end(), d->boundArgs.begin(), d->boundArgs.end());
        if (argc > 0) args.insert(args.end(), argv, argv + argc);
        return ctx->construct(d->boundTarget, static_cast<size_t>(args.size()),
                              args.data());
    }

    if (Value* err = checkMinArgc(ctx, d, argc)) return err;

    // 构造钩子：call/construct 语义不同的内建（包装类）走专用路径。
    // 钩子负责建对象、挂原型、设置内部槽，并返回对象（或 Exception/Fatal）。
    if (d->constructFn != nullptr) {
        return invokeNativeFunction(ctx, self, nullptr, argc, argv, d->constructFn);
    }

    // 取 fn.prototype（沿原型链查找，与 JS 一致）
    Value* proto = ctx->getProperty(self, "prototype");
    TypeKind pk = proto->type()->kind();
    if (!detail::isObjectKind(pk)) {
        // 非对象原型不显式挂链：新对象保留 createObject 的默认原型（Object.prototype）
        proto = nullptr;
    }

    // 新建 this 对象并挂原型
    Value* thisObj = ctx->createObject();
    if (thisObj == ctx->oomValue()) return thisObj;
    if (proto != nullptr) {
        Value* r = ctx->setPrototype(thisObj, proto);
        if (r->type()->kind() == TypeKind::Exception) return r;
    }

    Value* res = invokeNativeFunction(ctx, self, thisObj, argc, argv);
    if (res == nullptr) return thisObj;
    // 异常/致命错误直接透传（new 失败）
    TypeKind rk = res->type()->kind();
    if (rk == TypeKind::Exception || rk == TypeKind::Fatal) return res;
    // 构造函数显式返回对象（含迭代器）则用它，否则用新建的 this
    if (detail::isObjectKind(rk)) return res;
    return thisObj;
}

// 属性/原型操作：FunctionData 派生自 ObjectData，直接委托 ObjectType 的实现
// （属性查找沿原型链、OrdinarySet、环检测等逻辑与普通对象完全一致）。
Value* FunctionType::getProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->getProperty(ctx, self, key);
}
Value* FunctionType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return getObjectType()->setProperty(ctx, self, key, value);
}
bool FunctionType::hasProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->hasProperty(ctx, self, key);
}
Value* FunctionType::deleteProperty(IContext* ctx, Value* self, Value* key) {
    return getObjectType()->deleteProperty(ctx, self, key);
}
Value* FunctionType::defineProperty(IContext* ctx, Value* self, Value* key,
                                    Value* value, bool writable, bool enumerable,
                                    bool configurable) {
    return getObjectType()->defineProperty(ctx, self, key, value, writable, enumerable,
                                           configurable);
}
Value* FunctionType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* getter,
                                    Value* setter, bool enumerable, bool configurable) {
    return getObjectType()->defineAccessor(ctx, self, key, getter, setter, enumerable,
                                           configurable);
}
Value* FunctionType::getPrototype(IContext* ctx, Value* self) {
    return getObjectType()->getPrototype(ctx, self);
}
Value* FunctionType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return getObjectType()->setPrototype(ctx, self, proto);
}

}  // namespace neo::core
