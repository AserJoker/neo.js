// MessageValue.cpp - JS Value ↔ MessageValue 深拷贝
//
// W2 的克隆范围是 JSON-like：undefined/null/boolean/number/string/array/普通对象。
// 函数、symbol、bigint、Proxy、Date/Map/Set/RegExp/TypedArray 等暂不支持，
// 抛 DataCloneError（W3 扩展）。
//
// 循环引用同样抛 DataCloneError：用 path 集合记录当前递归路径（不是全局 visited，
// 因为 JSON-like 语义下重复的非循环引用应各自复制，而非共享）。
//
// 关联 ADR: 006（异常即值）、028（infra）

#include "worker/MessageClone.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/Boolean.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"

#include <string>
#include <unordered_set>
#include <utility>

namespace neo::worker {

namespace {

using core::ArrayData;
using core::BooleanData;
using core::IContext;
using core::NumberData;
using core::StringData;
using core::TypeKind;
using core::Value;

Value* dataCloneError(IContext* ctx, std::string message) {
    Value* err = ctx->createError(message, "DataCloneError");
    if (err == ctx->oomValue()) return err;
    // createError 的 name 只用于 stack 首行；DataCloneError 不是 ES 内建错误族，
    // 显式定义自有 name 属性，便于脚本按 err.name 区分。
    Value* name = ctx->createString("DataCloneError");
    if (name == ctx->oomValue()) return name;
    Value* defined = ctx->defineProperty(err, "name", name, true, false, true);
    if (core::detail::isErrorValue(defined)) return defined;
    return ctx->createException(err);
}

Value* cloneValue(IContext* ctx, Value* value, MessageValue& out,
                  std::unordered_set<const Value*>& path);

Value* cloneArray(IContext* ctx, Value* value, MessageValue& out,
                  std::unordered_set<const Value*>& path) {
    if (!path.insert(value).second) {
        return dataCloneError(ctx, "Message cannot contain cyclic references");
    }

    auto* data = static_cast<ArrayData*>(value->data());
    MessageValue result;
    result.kind = MessageValueKind::Array;
    result.array.reserve(data->length());

    for (uint32_t i = 0; i < data->length(); ++i) {
        if (!data->hasElement(i)) {
            result.array.push_back(MessageValue::hole());
            continue;
        }
        Value* key = ctx->createNumber(static_cast<double>(i));
        if (key == ctx->oomValue()) {
            path.erase(value);
            return key;
        }
        Value* elem = ctx->getProperty(value, key);
        if (core::detail::isErrorValue(elem)) {
            path.erase(value);
            return elem;
        }
        MessageValue child;
        if (Value* err = cloneValue(ctx, elem, child, path)) {
            path.erase(value);
            return err;
        }
        result.array.push_back(std::move(child));
    }

    path.erase(value);
    out = std::move(result);
    return nullptr;
}

Value* cloneObject(IContext* ctx, Value* value, MessageValue& out,
                   std::unordered_set<const Value*>& path) {
    if (!path.insert(value).second) {
        return dataCloneError(ctx, "Message cannot contain cyclic references");
    }

    Value* keys = ctx->ownEnumerableStringKeys(value);
    if (core::detail::isErrorValue(keys)) {
        path.erase(value);
        return keys;
    }

    auto* keyData = static_cast<ArrayData*>(keys->data());
    MessageValue result;
    result.kind = MessageValueKind::Object;
    result.object.reserve(keyData->length());

    for (uint32_t i = 0; i < keyData->length(); ++i) {
        Value* key = keyData->elementAt(i);
        if (key == nullptr) continue;
        auto* keyString = static_cast<StringData*>(key->data());

        Value* elem = ctx->getProperty(value, key);
        if (core::detail::isErrorValue(elem)) {
            path.erase(value);
            return elem;
        }
        MessageValue child;
        if (Value* err = cloneValue(ctx, elem, child, path)) {
            path.erase(value);
            return err;
        }
        result.object.emplace_back(keyString->value, std::move(child));
    }

    path.erase(value);
    out = std::move(result);
    return nullptr;
}

Value* cloneValue(IContext* ctx, Value* value, MessageValue& out,
                  std::unordered_set<const Value*>& path) {
    switch (value->type()->kind()) {
        case TypeKind::Undefined:
            out = MessageValue::undefined();
            return nullptr;
        case TypeKind::Null:
            out = MessageValue::null();
            return nullptr;
        case TypeKind::Boolean:
            out = MessageValue::booleanValue(
                static_cast<BooleanData*>(value->data())->value);
            return nullptr;
        case TypeKind::Number:
            out = MessageValue::numberValue(
                static_cast<NumberData*>(value->data())->value);
            return nullptr;
        case TypeKind::String:
            out = MessageValue::stringValue(
                static_cast<StringData*>(value->data())->value);
            return nullptr;
        case TypeKind::Array:
            return cloneArray(ctx, value, out, path);
        case TypeKind::Object:
            return cloneObject(ctx, value, out, path);
        default:
            return dataCloneError(
                ctx, std::string("Value of type '") +
                         value->type()->name() + "' cannot be cloned");
    }
}

}  // namespace

Value* valueToMessage(IContext* ctx, Value* value, MessageValue& out) {
    std::unordered_set<const Value*> path;
    return cloneValue(ctx, value, out, path);
}

Value* messageToValue(IContext* ctx, const MessageValue& value) {
    switch (value.kind) {
        case MessageValueKind::Undefined:
            return ctx->createUndefined();
        case MessageValueKind::Null:
            return ctx->createNull();
        case MessageValueKind::Boolean:
            return ctx->createBoolean(value.boolean);
        case MessageValueKind::Number:
            return ctx->createNumber(value.number);
        case MessageValueKind::String:
            return ctx->createString(value.string);
        case MessageValueKind::Hole:
            // Hole 只出现在数组元素位置；独立出现时按 undefined 处理。
            return ctx->createUndefined();
        case MessageValueKind::Array: {
            Value* array = ctx->createArray();
            if (array == ctx->oomValue()) return array;
            for (size_t i = 0; i < value.array.size(); ++i) {
                if (value.array[i].kind == MessageValueKind::Hole) continue;
                Value* elem = messageToValue(ctx, value.array[i]);
                if (core::detail::isErrorValue(elem)) return elem;
                Value* key = ctx->createNumber(static_cast<double>(i));
                if (key == ctx->oomValue()) return key;
                Value* set = ctx->setProperty(array, key, elem);
                if (core::detail::isErrorValue(set)) return set;
            }
            // 末尾空洞不会通过索引写入扩展 length，显式补齐。
            Value* len = ctx->createNumber(static_cast<double>(value.array.size()));
            if (len == ctx->oomValue()) return len;
            Value* setLen = ctx->setProperty(array, "length", len);
            if (core::detail::isErrorValue(setLen)) return setLen;
            return array;
        }
        case MessageValueKind::Object: {
            Value* object = ctx->createObject();
            if (object == ctx->oomValue()) return object;
            for (const auto& [key, child] : value.object) {
                Value* elem = messageToValue(ctx, child);
                if (core::detail::isErrorValue(elem)) return elem;
                Value* define = ctx->defineProperty(object, key, elem);
                if (core::detail::isErrorValue(define)) return define;
            }
            return object;
        }
    }
    return ctx->createUndefined();
}

}  // namespace neo::worker
