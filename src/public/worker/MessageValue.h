// MessageValue.h - 跨线程消息的结构化值（JSON-like）
//
// MessageValue 是与 Context/GC 无关的纯 C++ 值树，可在任意线程构造与销毁。
// postMessage 时把 JS Value 深拷贝成 MessageValue，接收方再还原成自己的 Value；
// 两边不共享任何 Value*/data。
//
// W2 支持：undefined / null / boolean / number / string / array / 普通对象。
// 函数、symbol、bigint、Proxy 等暂不支持，抛 DataCloneError（W3 扩展 Date/Map/
// Set/RegExp/TypedArray、循环引用与 transfer）。
//
// Hole 只出现在数组元素位置，用于保留稀疏数组的空洞（还原时跳过该索引）。
//
// 关联 ADR: 010（接口-实现-工厂）、028（infra）

#ifndef NEO_WORKER_MESSAGEVALUE_H
#define NEO_WORKER_MESSAGEVALUE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace neo::worker {

enum class MessageValueKind : uint8_t {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
    Hole,  // 仅用于数组元素位置
};

struct MessageValue {
    MessageValueKind kind = MessageValueKind::Undefined;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<MessageValue> array;
    std::vector<std::pair<std::string, MessageValue>> object;

    static MessageValue undefined() { return MessageValue{}; }
    static MessageValue null() {
        MessageValue v;
        v.kind = MessageValueKind::Null;
        return v;
    }
    static MessageValue booleanValue(bool b) {
        MessageValue v;
        v.kind = MessageValueKind::Boolean;
        v.boolean = b;
        return v;
    }
    static MessageValue numberValue(double n) {
        MessageValue v;
        v.kind = MessageValueKind::Number;
        v.number = n;
        return v;
    }
    static MessageValue stringValue(std::string s) {
        MessageValue v;
        v.kind = MessageValueKind::String;
        v.string = std::move(s);
        return v;
    }
    static MessageValue arrayValue(std::vector<MessageValue> elems) {
        MessageValue v;
        v.kind = MessageValueKind::Array;
        v.array = std::move(elems);
        return v;
    }
    static MessageValue objectValue(
        std::vector<std::pair<std::string, MessageValue>> props) {
        MessageValue v;
        v.kind = MessageValueKind::Object;
        v.object = std::move(props);
        return v;
    }
    static MessageValue hole() {
        MessageValue v;
        v.kind = MessageValueKind::Hole;
        return v;
    }
};

}  // namespace neo::worker

#endif  // NEO_WORKER_MESSAGEVALUE_H
