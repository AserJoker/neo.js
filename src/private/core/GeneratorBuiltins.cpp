// GeneratorBuiltins.cpp - %GeneratorPrototype% 与 next/return/throw（ADR-045 §8）
//
// 语义（ES2024 §27.5.3）：
//   next(v)   —— GeneratorResume(coroutine, v)；首次 next 的 v 被丢弃。
//   return(v) —— GeneratorResumeAbrupt(Return)：未启动则直接完成；
//                已挂起则注入 Interrupt(Return, resumePoint, v)。注入值就是恢复后
//                yield 表达式的计算值，finally 由函数体正常的 try/finally 控制流处理
//                （native 在每个 case 开头检查 sentValue），生成器层不特判。
//   throw(e)  —— 同理注入 Interrupt(Throw, resumePoint, createException(e))。
//
// 恢复结果解释：
//   - Interrupt(Yield) → { value, done:false }
//   - 其他 Interrupt    → { value, done:true }
//   - Exception/Fatal   → 原样透传（调用方按异常处理）
//   - 普通值            → { value, done:true }
//
// 关联 ADR: 020（标准库组织）、034（原型-构造函数）、045（协程抽象）、046（Interrupt）

#include "core/GeneratorBuiltins.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Coroutine.h"
#include "core/types/Exception.h"
#include "core/types/Interrupt.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

namespace {

// 迭代器结果对象 { value, done }（ES2024 §7.4.1 CreateIterResultObject）。
Value* createIterResult(IContext* ctx, Value* value, bool done) {
    Value* result = ctx->createObject();
    if (result == ctx->oomValue()) return result;
    Value* r = ctx->defineProperty(result, "value", value, true, true, true);
    if (detail::isErrorValue(r)) return r;
    r = ctx->defineProperty(result, "done", ctx->createBoolean(done), true, true, true);
    return detail::isErrorValue(r) ? r : result;
}

// 把 resumeCoroutine 的原始结果解释成迭代器结果（异常原样透传）。
Value* interpretResume(IContext* ctx, Value* res) {
    TypeKind k = res->type()->kind();
    if (k == TypeKind::Exception || k == TypeKind::Fatal) return res;
    if (detail::isInterruptValue(res)) {
        InterruptData* ir = asInterrupt(res);
        // yield* 委托：载荷已是迭代器结果对象，原样返回（§14.4.14）。
        if (ir->kind == InterruptKind::Yield && ir->delegateResult) {
            return ir->value;
        }
        return createIterResult(ctx, ir->value, ir->kind != InterruptKind::Yield);
    }
    return createIterResult(ctx, res, true);
}

// 协程是否尚未启动（首次 next 前）：Suspended 且没有执行作用域。
bool isUnstarted(const CoroutineData* co) {
    return co->state == CoroutineState::Suspended && co->execScope == nullptr;
}

Value* throwValue(IContext* ctx, Value* e) {
    // 内部已是 Exception 通道值则原样透传，否则包装成可捕获异常。
    if (e->type()->kind() == TypeKind::Exception) return e;
    return ctx->createException(e);
}

// —— %GeneratorPrototype%.next(v)（ES2024 §27.5.3.2）——
Value* generatorNext(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CoroutineData* co = asCoroutine(self);
    if (co == nullptr) {
        return detail::typeError(ctx, "Generator method called on incompatible receiver");
    }

    if (co->state == CoroutineState::Completed) {
        return createIterResult(ctx, ctx->createUndefined(), true);
    }
    // 首次 next 的实参按规范被丢弃。
    Value* sent = (!isUnstarted(co) && argc > 0) ? argv[0] : ctx->createUndefined();
    return interpretResume(ctx, ctx->resumeCoroutine(self, sent));
}

// —— %GeneratorPrototype%.return(v)（ES2024 §27.5.3.4）——
Value* generatorReturn(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CoroutineData* co = asCoroutine(self);
    if (co == nullptr) {
        return detail::typeError(ctx, "Generator method called on incompatible receiver");
    }

    Value* v = argc > 0 ? argv[0] : ctx->createUndefined();
    if (co->state == CoroutineState::Completed) {
        return createIterResult(ctx, v, true);
    }
    if (isUnstarted(co)) {
        // 未启动：不运行函数体，直接完成。
        co->state = CoroutineState::Completed;
        co->yielded = v;
        return createIterResult(ctx, v, true);
    }
    Value* sig = ctx->createInterrupt(InterruptKind::Return, co->resumePoint, v);
    return interpretResume(ctx, ctx->resumeCoroutine(self, sig));
}

// —— %GeneratorPrototype%.throw(e)（ES2024 §27.5.3.3）——
Value* generatorThrow(IContext* ctx, Value* self, size_t argc, Value* argv[]) {
    CoroutineData* co = asCoroutine(self);
    if (co == nullptr) {
        return detail::typeError(ctx, "Generator method called on incompatible receiver");
    }

    Value* e = argc > 0 ? argv[0] : ctx->createUndefined();
    if (co->state == CoroutineState::Completed) {
        return throwValue(ctx, e);
    }
    if (isUnstarted(co)) {
        // 未启动：不运行函数体，直接以 e 结束并抛出。
        co->state = CoroutineState::Completed;
        co->yielded = e;
        return throwValue(ctx, e);
    }
    Value* sig = ctx->createInterrupt(InterruptKind::Throw, co->resumePoint,
                                      throwValue(ctx, e));
    return interpretResume(ctx, ctx->resumeCoroutine(self, sig));
}

// —— %GeneratorPrototype%[@@iterator]（ES2024 §27.5.3.1）——
Value* generatorIterator(IContext* /*ctx*/, Value* self, size_t /*argc*/,
                         Value* /*argv*/[]) {
    return self;
}

// %GeneratorFunction%(...args)（ES2024 §27.3.1）：前 n-1 个参数是形参源码，
// 最后一个参数是函数体源码，在全局环境创建生成器函数（不捕获调用方作用域）。
Value* generatorFunctionCtor(IContext* ctx, Value* /*self*/, size_t argc,
                             Value* argv[]) {
    std::string params;
    std::string body;
    for (size_t i = 0; i < argc; ++i) {
        Value* text = ctx->toString(argv[i]);
        if (detail::isErrorValue(text)) return text;
        const std::string& str = static_cast<StringData*>(text->data())->value;
        if (i + 1 == argc) {
            body = str;
        } else {
            if (!params.empty()) params += ",";
            params += str;
        }
    }
    // 复用编译管线：生成器函数表达式会走 setFunctionGenerator 挂
    // %GeneratorFunction.prototype% 与 %GeneratorPrototype%。
    std::string source =
        "(function* anonymous(" + params + "\n) {\n" + body + "\n})";
    Value* src = ctx->createString(source);
    if (src == ctx->oomValue()) return src;
    return ctx->evalSource(src, nullptr, false);
}

// 定义原型方法（writable / 不可枚举 / 可配置），length 显式覆盖。
bool defineMethod(IContext* ctx, Value* obj, const char* name, NativeFn fn,
                  size_t length) {
    Value* m = ctx->createNativeFunction(fn, 0);
    if (m == ctx->oomValue()) return false;
    ctx->setFunctionName(m, name);
    Value* len = ctx->createNumber(static_cast<double>(length));
    if (len == ctx->oomValue()) return false;
    if (ctx->defineProperty(m, "length", len, false, false, true)->type()->kind() ==
        TypeKind::Exception) {
        return false;
    }
    return ctx->defineProperty(obj, name, m, true, false, true)->type()->kind() !=
           TypeKind::Exception;
}

}  // namespace

bool registerGeneratorBuiltins(IContext* ctx) {
    // —— %GeneratorPrototype%（普通对象，原型 = %Iterator.prototype%）——
    Value* genProto = ctx->createObject();
    if (genProto == ctx->oomValue()) return false;
    if (Value* iterProto = ctx->iteratorPrototype()) {
        if (ctx->setPrototype(genProto, iterProto)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    ctx->setGeneratorPrototype(genProto);

    // —— %GeneratorFunction.prototype%（普通对象，原型 = %Function.prototype%）——
    // 生成器函数对象的 [[Prototype]]（ES2024 §27.5.1.2）。
    Value* genFuncProto = ctx->createObject();
    if (genFuncProto == ctx->oomValue()) return false;
    if (Value* fnProto = ctx->functionPrototype()) {
        if (ctx->setPrototype(genFuncProto, fnProto)->type()->kind() ==
            TypeKind::Exception) {
            return false;
        }
    }
    ctx->setGeneratorFunctionPrototype(genFuncProto);
    // prototype：%GeneratorPrototype%，writable=false / enumerable=false /
    // configurable=true（§27.3.3.2）。
    if (ctx->defineProperty(genFuncProto, "prototype", genProto, false, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— %GeneratorFunction%（§27.3.1）——
    // 生成器函数对象的 constructor；调用/构造均走动态源码编译。
    Value* genFuncCtor = ctx->createNativeFunction(generatorFunctionCtor, 0);
    if (genFuncCtor == ctx->oomValue()) return false;
    ctx->setFunctionName(genFuncCtor, "GeneratorFunction");
    ctx->setFunctionConstructor(genFuncCtor, generatorFunctionCtor);
    if (ctx->defineProperty(genFuncCtor, "length", ctx->createNumber(1.0), false,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // %GeneratorFunction%.prototype = %GeneratorFunction.prototype%
    // （writable=false / enumerable=false / configurable=false）。
    if (ctx->defineProperty(genFuncCtor, "prototype", genFuncProto, false, false,
                            false)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // %GeneratorFunction.prototype%.constructor = %GeneratorFunction%
    // （writable=false / enumerable=false / configurable=true）。
    if (ctx->defineProperty(genFuncProto, "constructor", genFuncCtor, false,
                            false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    // @@toStringTag = "GeneratorFunction"。
    if (ctx->defineProperty(genFuncProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("GeneratorFunction"), false, false,
                            true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— next / return / throw ——
    if (!defineMethod(ctx, genProto, "next", generatorNext, 1)) return false;
    if (!defineMethod(ctx, genProto, "return", generatorReturn, 1)) return false;
    if (!defineMethod(ctx, genProto, "throw", generatorThrow, 1)) return false;

    // —— @@iterator：返回 this ——
    Value* iterMethod = ctx->createNativeFunction(generatorIterator, 0);
    if (iterMethod == ctx->oomValue()) return false;
    ctx->setFunctionName(iterMethod, "[Symbol.iterator]");
    if (ctx->defineProperty(genProto, ctx->getWellKnownSymbol(WellKnownSymbol::Iterator),
                            iterMethod, true, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }

    // —— @@toStringTag = "Generator" ——
    if (ctx->defineProperty(genProto,
                            ctx->getWellKnownSymbol(WellKnownSymbol::ToStringTag),
                            ctx->createString("Generator"), false, false, true)
            ->type()->kind() == TypeKind::Exception) {
        return false;
    }
    return true;
}

}  // namespace neo::core
