// TestHelpers.h - 测试共享辅助
//
// hasProperty 返回 Value*（Boolean 或 Exception/Fatal）；测试通常只关心
// 布尔结果，这里提供一个显式命名的便捷读取器。

#ifndef NEO_TESTS_TEST_HELPERS_H
#define NEO_TESTS_TEST_HELPERS_H

#include "core/IContext.h"
#include "core/Value.h"
#include "core/types/Boolean.h"

#include <string_view>

namespace neo::test {

using neo::core::BooleanData;
using neo::core::IContext;
using neo::core::TypeKind;
using neo::core::Value;

inline bool hasProperty(IContext* ctx, Value* obj, Value* key) {
    Value* r = ctx->hasProperty(obj, key);
    return r->type()->kind() == TypeKind::Boolean &&
           static_cast<BooleanData*>(r->data())->value;
}

inline bool hasProperty(IContext* ctx, Value* obj, std::string_view key) {
    return hasProperty(ctx, obj, ctx->createString(key));
}

// 测试里 ctx 常是 std::unique_ptr<IContext>。
template <typename CtxPtr>
inline bool hasProperty(const CtxPtr& ctx, Value* obj, Value* key) {
    return hasProperty(ctx.get(), obj, key);
}

template <typename CtxPtr>
inline bool hasProperty(const CtxPtr& ctx, Value* obj, std::string_view key) {
    return hasProperty(ctx.get(), obj, key);
}

}  // namespace neo::test

#endif  // NEO_TESTS_TEST_HELPERS_H
