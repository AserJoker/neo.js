// MessageValue.cpp - JS Value ↔ MessageValue 结构化克隆
//
// 对象图用 id + Reference 表示：首次访问对象时分配 id 并登记 seen，
// 再次访问同一 Value 时产出 Reference，从而支持循环引用与共享引用。
//
// 支持：原始值、Array、普通对象、Date、Map、Set、RegExp、ArrayBuffer、
// SharedArrayBuffer（共享块引用）、DataView、TypedArray。函数/symbol/
// bigint/Proxy/WeakMap/WeakSet 等抛 DataCloneError。
//
// 关联 ADR: 006（异常即值）、028（infra）

#include "worker/MessageClone.h"

#include "core/IContext.h"
#include "core/IType.h"
#include "core/PortBuiltins.h"
#include "core/Value.h"
#include "core/types/Array.h"
#include "core/types/ArrayBuffer.h"
#include "core/types/Boolean.h"
#include "core/types/Collection.h"
#include "core/types/DataView.h"
#include "core/types/Date.h"
#include "core/types/MessagePort.h"
#include "core/types/Null.h"
#include "core/types/Number.h"
#include "core/types/RegExp.h"
#include "core/types/String.h"
#include "core/types/TypeBehavior.h"
#include "core/types/TypedArray.h"
#include "worker/Port.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace neo::worker {

namespace {

using core::ArrayData;
using core::BooleanData;
using core::CollectionData;
using core::CollectionKind;
using core::ErrorKind;
using core::IContext;
using core::NumberData;
using core::StringData;
using core::TypeKind;
using core::TypedArrayKind;
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

// 转移列表按 data 身份匹配：跨域复制后 Value* 可能不同（ADR-065 §4）。
bool containsIdentity(const std::vector<Value*>& list, Value* value) {
    for (Value* item : list) {
        if (item != nullptr && item->data() == value->data()) return true;
    }
    return false;
}

struct CloneContext {
    IContext* ctx;
    // 按 data 身份判重：跨域复制后同一对象可能有多个 Value*（ADR-065 §4）。
    std::unordered_map<const void*, uint32_t> seen;
    uint32_t nextId = 1;
    const std::vector<Value*>* transfer = nullptr;
    std::vector<Value*>* transferred = nullptr;
};

Value* cloneValue(IContext* ctx, Value* value, MessageValue& out,
                  CloneContext& cc);

// 读取 Error 的字符串属性；undefined 视为空。转换失败原样返回错误值。
Value* readStringProp(IContext* ctx, Value* object, std::string_view key,
                      std::string& out) {
    Value* v = ctx->getProperty(object, key);
    if (core::detail::isErrorValue(v)) return v;
    out.clear();
    if (v->type()->kind() == TypeKind::Undefined) return nullptr;
    Value* s = ctx->toString(v);
    if (core::detail::isErrorValue(s)) return s;
    out = static_cast<StringData*>(s->data())->value;
    return nullptr;
}

// 对象类节点：已见过则产出 Reference 并返回 true；否则分配 id 写入 out。
bool beginNode(CloneContext& cc, const Value* value, MessageValue& out,
               MessageValueKind kind) {
    auto it = cc.seen.find(value->data());
    if (it != cc.seen.end()) {
        out = MessageValue::reference(it->second);
        return true;
    }
    out.kind = kind;
    out.id = cc.nextId++;
    cc.seen.emplace(value->data(), out.id);
    return false;
}

Value* cloneArray(IContext* ctx, Value* value, MessageValue& out,
                  CloneContext& cc) {
    if (beginNode(cc, value, out, MessageValueKind::Array)) return nullptr;

    auto* data = static_cast<ArrayData*>(value->data());
    out.array.reserve(data->length());
    for (uint32_t i = 0; i < data->length(); ++i) {
        if (!data->hasElement(i)) {
            out.array.push_back(MessageValue::hole());
            continue;
        }
        Value* key = ctx->createNumber(static_cast<double>(i));
        if (key == ctx->oomValue()) return key;
        Value* elem = ctx->getProperty(value, key);
        if (core::detail::isErrorValue(elem)) return elem;
        MessageValue child;
        if (Value* err = cloneValue(ctx, elem, child, cc)) return err;
        out.array.push_back(std::move(child));
    }
    return nullptr;
}

Value* cloneObject(IContext* ctx, Value* value, MessageValue& out,
                   CloneContext& cc) {
    if (beginNode(cc, value, out, MessageValueKind::Object)) return nullptr;

    Value* keys = ctx->ownEnumerableStringKeys(value);
    if (core::detail::isErrorValue(keys)) return keys;

    auto* keyData = static_cast<ArrayData*>(keys->data());
    out.object.reserve(keyData->length());
    for (uint32_t i = 0; i < keyData->length(); ++i) {
        Value* key = keyData->elementAt(i);
        if (key == nullptr) continue;
        auto* keyString = static_cast<StringData*>(key->data());

        Value* elem = ctx->getProperty(value, key);
        if (core::detail::isErrorValue(elem)) return elem;
        MessageValue child;
        if (Value* err = cloneValue(ctx, elem, child, cc)) return err;
        out.object.emplace_back(keyString->value, std::move(child));
    }
    return nullptr;
}

Value* cloneCollection(IContext* ctx, Value* value, MessageValue& out,
                       CloneContext& cc) {
    auto* data = core::asCollection(value);
    if (data == nullptr) {
        return dataCloneError(ctx, "Invalid collection");
    }
    if (data->kind == CollectionKind::WeakMap ||
        data->kind == CollectionKind::WeakSet) {
        return dataCloneError(ctx, "WeakMap/WeakSet cannot be cloned");
    }

    const bool isMap = data->kind == CollectionKind::Map;
    if (beginNode(cc, value, out,
                  isMap ? MessageValueKind::Map : MessageValueKind::Set)) {
        return nullptr;
    }

    if (isMap) {
        out.entries.reserve(data->entries.size());
        for (const auto& entry : data->entries) {
            MessageValue key;
            if (Value* err = cloneValue(ctx, entry.key, key, cc)) return err;
            MessageValue val;
            if (Value* err = cloneValue(ctx, entry.value, val, cc)) return err;
            out.entries.emplace_back(std::move(key), std::move(val));
        }
    } else {
        out.items.reserve(data->entries.size());
        for (const auto& entry : data->entries) {
            MessageValue item;
            if (Value* err = cloneValue(ctx, entry.key, item, cc)) return err;
            out.items.push_back(std::move(item));
        }
    }
    return nullptr;
}

Value* cloneValue(IContext* ctx, Value* value, MessageValue& out,
                  CloneContext& cc) {
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
            return cloneArray(ctx, value, out, cc);
        case TypeKind::Object:
            return cloneObject(ctx, value, out, cc);
        case TypeKind::Date: {
            if (beginNode(cc, value, out, MessageValueKind::Date)) return nullptr;
            out.number = core::asDate(value)->timeValue;
            return nullptr;
        }
        case TypeKind::Collection:
            return cloneCollection(ctx, value, out, cc);
        case TypeKind::RegExp: {
            if (beginNode(cc, value, out, MessageValueKind::RegExp)) {
                return nullptr;
            }
            auto* data = core::regexpData(value);
            out.string = data->source;
            out.string2 = data->flags;
            return nullptr;
        }
        case TypeKind::ArrayBuffer: {
            auto* data = core::asArrayBuffer(value);
            if (data->detached) {
                return dataCloneError(ctx, "Detached ArrayBuffer cannot be cloned");
            }
            if (beginNode(cc, value, out, MessageValueKind::ArrayBuffer)) {
                return nullptr;
            }
            // 转移与复制都先把字节放进消息；源对象在克隆成功后才分离
            // （克隆失败时不能产生副作用）。
            out.bytes = data->bytes;
            if (cc.transfer != nullptr &&
                containsIdentity(*cc.transfer, value)) {
                if (cc.transferred != nullptr) cc.transferred->push_back(value);
            }
            return nullptr;
        }
        case TypeKind::SharedArrayBuffer: {
            auto* data = core::asSharedArrayBuffer(value);
            if (data == nullptr || !data->shared) {
                return dataCloneError(ctx, "Invalid SharedArrayBuffer");
            }
            if (beginNode(cc, value, out, MessageValueKind::SharedArrayBuffer)) {
                return nullptr;
            }
            // 共享块不复制内存：消息持有一个 shared_ptr 引用。
            out.shared = data->shared;
            return nullptr;
        }
        case TypeKind::DataView: {
            if (beginNode(cc, value, out, MessageValueKind::DataView)) {
                return nullptr;
            }
            auto* data = core::asDataView(value);
            out.byteOffset = data->byteOffset;
            out.byteLength = data->byteLength;
            out.buffer.resize(1);
            return cloneValue(ctx, data->buffer, out.buffer[0], cc);
        }
        case TypeKind::Error: {
            if (beginNode(cc, value, out, MessageValueKind::Error)) {
                return nullptr;
            }
            if (Value* err = readStringProp(ctx, value, "name", out.string)) {
                return err;
            }
            if (Value* err =
                    readStringProp(ctx, value, "message", out.string2)) {
                return err;
            }
            if (Value* err = readStringProp(ctx, value, "stack", out.stack)) {
                return err;
            }
            Value* cause = ctx->getProperty(value, "cause");
            if (core::detail::isErrorValue(cause)) return cause;
            if (cause->type()->kind() != TypeKind::Undefined) {
                out.cause.resize(1);
                if (Value* err = cloneValue(ctx, cause, out.cause[0], cc)) {
                    return err;
                }
            }
            return nullptr;
        }
        case TypeKind::MessagePort: {
            if (cc.transfer == nullptr ||
                !containsIdentity(*cc.transfer, value)) {
                return dataCloneError(ctx,
                                      "MessagePort must be transferred");
            }
            auto* data = core::asMessagePort(value);
            if (data->detached) {
                return dataCloneError(ctx, "MessagePort is already detached");
            }
            if (beginNode(cc, value, out, MessageValueKind::MessagePort)) {
                return nullptr;
            }
            out.port = data->endpoint;
            return nullptr;
        }
        case TypeKind::TypedArray: {
            if (beginNode(cc, value, out, MessageValueKind::TypedArray)) {
                return nullptr;
            }
            auto* data = core::asTypedArray(value);
            out.byteOffset = data->byteOffset;
            out.length = data->length;
            out.typedArrayKind = static_cast<uint8_t>(data->kind);
            out.buffer.resize(1);
            return cloneValue(ctx, data->buffer, out.buffer[0], cc);
        }
        default:
            return dataCloneError(
                ctx, std::string("Value of type '") +
                         value->type()->name() + "' cannot be cloned");
    }
}

struct RestoreContext {
    IContext* ctx;
    std::unordered_map<uint32_t, Value*> seen;
};

Value* restoreValue(IContext* ctx, const MessageValue& value,
                    RestoreContext& rc);

Value* restoreArray(IContext* ctx, const MessageValue& value,
                    RestoreContext& rc) {
    Value* array = ctx->createArray();
    if (array == ctx->oomValue()) return array;
    rc.seen[value.id] = array;

    for (size_t i = 0; i < value.array.size(); ++i) {
        if (value.array[i].kind == MessageValueKind::Hole) continue;
        Value* elem = restoreValue(ctx, value.array[i], rc);
        if (core::detail::isErrorValue(elem)) return elem;
        Value* key = ctx->createNumber(static_cast<double>(i));
        if (key == ctx->oomValue()) return key;
        Value* set = ctx->setProperty(array, key, elem);
        if (core::detail::isErrorValue(set)) return set;
    }
    Value* len = ctx->createNumber(static_cast<double>(value.array.size()));
    if (len == ctx->oomValue()) return len;
    Value* setLen = ctx->setProperty(array, "length", len);
    if (core::detail::isErrorValue(setLen)) return setLen;
    return array;
}

Value* restoreObject(IContext* ctx, const MessageValue& value,
                     RestoreContext& rc) {
    Value* object = ctx->createObject();
    if (object == ctx->oomValue()) return object;
    rc.seen[value.id] = object;

    for (const auto& [key, child] : value.object) {
        Value* elem = restoreValue(ctx, child, rc);
        if (core::detail::isErrorValue(elem)) return elem;
        Value* define = ctx->defineProperty(object, key, elem);
        if (core::detail::isErrorValue(define)) return define;
    }
    return object;
}

Value* restoreCollection(IContext* ctx, const MessageValue& value,
                         RestoreContext& rc) {
    const bool isMap = value.kind == MessageValueKind::Map;
    Value* collection = ctx->createCollection(
        isMap ? CollectionKind::Map : CollectionKind::Set);
    if (collection == ctx->oomValue()) return collection;
    rc.seen[value.id] = collection;

    auto* data = core::asCollection(collection);
    if (isMap) {
        data->entries.reserve(value.entries.size());
        for (const auto& [keyNode, valNode] : value.entries) {
            Value* key = restoreValue(ctx, keyNode, rc);
            if (core::detail::isErrorValue(key)) return key;
            Value* val = restoreValue(ctx, valNode, rc);
            if (core::detail::isErrorValue(val)) return val;
            data->entries.push_back(core::CollectionEntry{key, val});
        }
    } else {
        data->entries.reserve(value.items.size());
        for (const auto& itemNode : value.items) {
            Value* item = restoreValue(ctx, itemNode, rc);
            if (core::detail::isErrorValue(item)) return item;
            data->entries.push_back(core::CollectionEntry{item, item});
        }
    }
    return collection;
}

Value* restoreValue(IContext* ctx, const MessageValue& value,
                    RestoreContext& rc) {
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
            return ctx->createUndefined();
        case MessageValueKind::Reference: {
            auto it = rc.seen.find(value.ref);
            if (it == rc.seen.end()) {
                return ctx->createErrorException(ErrorKind::Error,
                                                 "Invalid message reference");
            }
            return it->second;
        }
        case MessageValueKind::Array:
            return restoreArray(ctx, value, rc);
        case MessageValueKind::Object:
            return restoreObject(ctx, value, rc);
        case MessageValueKind::Date: {
            Value* date = ctx->createDate(value.number);
            if (date != ctx->oomValue()) rc.seen[value.id] = date;
            return date;
        }
        case MessageValueKind::Map:
        case MessageValueKind::Set:
            return restoreCollection(ctx, value, rc);
        case MessageValueKind::RegExp: {
            Value* regexp = ctx->createRegExp(value.string, value.string2);
            if (!core::detail::isErrorValue(regexp)) rc.seen[value.id] = regexp;
            return regexp;
        }
        case MessageValueKind::ArrayBuffer: {
            Value* buffer = ctx->createArrayBuffer(value.bytes.size());
            if (buffer == ctx->oomValue()) return buffer;
            rc.seen[value.id] = buffer;
            core::asArrayBuffer(buffer)->bytes = value.bytes;
            return buffer;
        }
        case MessageValueKind::SharedArrayBuffer: {
            if (!value.shared) {
                return ctx->createErrorException(ErrorKind::Error,
                                                 "Invalid shared buffer");
            }
            Value* buffer = ctx->createSharedArrayBuffer(value.shared);
            if (buffer != ctx->oomValue()) rc.seen[value.id] = buffer;
            return buffer;
        }
        case MessageValueKind::DataView: {
            if (value.buffer.empty()) {
                return ctx->createErrorException(ErrorKind::Error,
                                                 "Invalid message buffer");
            }
            Value* buffer = restoreValue(ctx, value.buffer[0], rc);
            if (core::detail::isErrorValue(buffer)) return buffer;
            Value* view = ctx->createDataView(buffer, value.byteOffset,
                                              value.byteLength);
            if (view != ctx->oomValue()) rc.seen[value.id] = view;
            return view;
        }
        case MessageValueKind::Error: {
            const std::string& name =
                value.string.empty() ? std::string("Error") : value.string;
            Value* err = ctx->createError(value.string2, name);
            if (err == ctx->oomValue()) return err;
            rc.seen[value.id] = err;

            Value* nameValue = ctx->createString(name);
            if (nameValue == ctx->oomValue()) return nameValue;
            Value* defined =
                ctx->defineProperty(err, "name", nameValue, true, false, true);
            if (core::detail::isErrorValue(defined)) return defined;

            if (!value.stack.empty()) {
                Value* stack = ctx->createString(value.stack);
                if (stack == ctx->oomValue()) return stack;
                Value* set =
                    ctx->defineProperty(err, "stack", stack, true, false, true);
                if (core::detail::isErrorValue(set)) return set;
            }
            if (!value.cause.empty()) {
                Value* cause = restoreValue(ctx, value.cause[0], rc);
                if (core::detail::isErrorValue(cause)) return cause;
                Value* set =
                    ctx->defineProperty(err, "cause", cause, true, false, true);
                if (core::detail::isErrorValue(set)) return set;
            }
            return err;
        }
        case MessageValueKind::MessagePort: {
            if (!value.port) {
                return ctx->createErrorException(ErrorKind::Error,
                                                 "Invalid message port");
            }
            Value* port = core::createTransferredPort(ctx, value.port);
            if (!core::detail::isErrorValue(port)) rc.seen[value.id] = port;
            return port;
        }
        case MessageValueKind::TypedArray: {
            if (value.buffer.empty()) {
                return ctx->createErrorException(ErrorKind::Error,
                                                 "Invalid message buffer");
            }
            Value* buffer = restoreValue(ctx, value.buffer[0], rc);
            if (core::detail::isErrorValue(buffer)) return buffer;
            Value* view = ctx->createTypedArray(
                buffer, value.byteOffset, value.length,
                static_cast<TypedArrayKind>(value.typedArrayKind));
            if (view != ctx->oomValue()) rc.seen[value.id] = view;
            return view;
        }
    }
    return ctx->createUndefined();
}

}  // namespace

Value* resolveTransferList(IContext* ctx, Value* list,
                           std::vector<Value*>& out) {
    if (list == nullptr) return nullptr;
    TypeKind kind = list->type()->kind();
    if (kind == TypeKind::Undefined || kind == TypeKind::Null) return nullptr;
    if (kind != TypeKind::Array) {
        return dataCloneError(ctx, "Transfer list must be an array");
    }

    auto* data = static_cast<ArrayData*>(list->data());
    for (uint32_t i = 0; i < data->length(); ++i) {
        Value* item = data->elementAt(i);
        if (item == nullptr) continue;
        TypeKind itemKind = item->type()->kind();
        if (itemKind == TypeKind::ArrayBuffer) {
            auto* buffer = core::asArrayBuffer(item);
            if (buffer->detached) {
                return dataCloneError(ctx, "ArrayBuffer is already detached");
            }
        } else if (itemKind == TypeKind::MessagePort) {
            auto* port = core::asMessagePort(item);
            if (port->detached) {
                return dataCloneError(ctx, "MessagePort is already detached");
            }
        } else {
            return dataCloneError(ctx, "Transferable type not supported");
        }
        if (containsIdentity(out, item)) {
            return dataCloneError(ctx, "Duplicate transferable");
        }
        out.push_back(item);
    }
    return nullptr;
}

void applyTransfer(const std::vector<Value*>& transfer) {
    for (Value* item : transfer) {
        TypeKind kind = item->type()->kind();
        if (kind == TypeKind::ArrayBuffer) {
            auto* data = core::asArrayBuffer(item);
            data->bytes.clear();
            data->detached = true;
        } else if (kind == TypeKind::MessagePort) {
            auto* data = core::asMessagePort(item);
            if (!data->detached) {
                data->detached = true;
                if (data->endpoint) data->endpoint->detachOwner();
            }
        }
    }
}

Value* valueToMessage(IContext* ctx, Value* value, MessageValue& out,
                      const std::vector<Value*>& transfer,
                      std::vector<Value*>* transferred) {
    CloneContext cc{ctx, {}, 1, &transfer, transferred};
    return cloneValue(ctx, value, out, cc);
}

Value* messageToValue(IContext* ctx, const MessageValue& value) {
    RestoreContext rc{ctx, {}};
    return restoreValue(ctx, value, rc);
}

}  // namespace neo::worker
