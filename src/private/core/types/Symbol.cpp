// Symbol.cpp - SymbolType 行为方法实现
//
// symbol 是原始类型，不能隐式转换为 number/string：
//   add/sub/mul/div —— ToPrimitive 后 ToString/ToNumber 抛 TypeError（ES2024 §7.1.4/§7.1.14）
//   toString        —— "Symbol(description)"（等价 sym.toString()）
//   call/construct  —— 不是函数/构造函数
//   属性/原型操作   —— 原始类型无自有属性、无原型（与 string/number 一致）
//
// 关联 ADR: 016（Type 体系）、017（类型清单）、006（异常即值）

#include "core/IContext.h"
#include "core/types/Symbol.h"
#include "core/types/TypeBehavior.h"

namespace neo::core {

using namespace detail;

// symbol 走统一转换链（ES2024 §13.15.3）：两侧 ToPrimitive 后，
// 任一为 string 则拼接——此时 ToString(symbol) 抛 TypeError；
// 否则 ToNumber(symbol) 抛 TypeError。两条路径都由共享辅助给出正确错误。
Value* SymbolType::add(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return addValues(ctx, a, b);
}
Value* SymbolType::sub(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '-', a, b);
}
Value* SymbolType::mul(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '*', a, b);
}
Value* SymbolType::div(IContext* ctx, Value* a, Value* b) {
    if (Value* p = passthrough(a, b)) return p;
    return numericOp(ctx, '/', a, b);
}

Value* SymbolType::toString(IContext* ctx, Value* self) {
    const auto& d = static_cast<SymbolData*>(self->data())->description;
    return ctx->createString("Symbol(" + d + ")");
}

Value* SymbolType::call(IContext* ctx, Value* self, Value* /*thisArg*/,
                        size_t /*argc*/, Value* /*argv*/[]) {
    return notAFunction(ctx, self);
}

Value* SymbolType::construct(IContext* ctx, Value* self, size_t /*argc*/, Value* /*argv*/[]) {
    return notAConstructor(ctx, self);
}

// 属性操作：原始类型无自有属性，读取返回 undefined，写入报错。
Value* SymbolType::getProperty(IContext* ctx, Value* self, Value* key) {
    return primitiveGetProperty(ctx, self, key);
}
Value* SymbolType::setProperty(IContext* ctx, Value* self, Value* key, Value* value) {
    return primitiveSetProperty(ctx, self, key, value);
}
bool SymbolType::hasProperty(IContext* /*ctx*/, Value* /*self*/, Value* /*key*/) {
    return false;
}
Value* SymbolType::deleteProperty(IContext* ctx, Value* /*self*/, Value* /*key*/) {
    // 不存在的属性 delete 视为成功（与 Object 一致）
    return ctx->createBoolean(true);
}
Value* SymbolType::defineProperty(IContext* ctx, Value* self, Value* key,
                                  Value* /*value*/, bool /*writable*/, bool /*enumerable*/,
                                  bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

// 原型操作：原始类型无原型。
Value* SymbolType::getPrototype(IContext* ctx, Value* self) {
    return primitiveGetPrototype(ctx, self);
}
Value* SymbolType::setPrototype(IContext* ctx, Value* self, Value* proto) {
    return primitiveSetPrototype(ctx, self, proto);
}


// 访问器属性定义（ADR-018）：原始类型无属性容器。
Value* SymbolType::defineAccessor(IContext* ctx, Value* self, Value* key, Value* /*getter*/,
                               Value* /*setter*/, bool /*enumerable*/, bool /*configurable*/) {
    return primitiveSetProperty(ctx, self, key, nullptr);
}

}  // namespace neo::core
