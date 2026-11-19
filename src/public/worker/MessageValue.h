// MessageValue.h - 跨线程消息的结构化值
//
// MessageValue 是与 Context/GC 无关的纯 C++ 值树/图，可在任意线程构造与销毁。
// postMessage 时把 JS Value 深拷贝成 MessageValue，接收方再还原成自己的 Value；
// 两边不共享任何 Value*/data。
//
// 支持：undefined / null / boolean / number / string / array / 普通对象 /
// Date / Map / Set / RegExp / ArrayBuffer / DataView / TypedArray，
// 以及循环引用与共享引用（对象图用 id + Reference 表示）。
//
// 不支持：函数、symbol、bigint、Proxy、WeakMap/WeakSet、Error 等，抛
// DataCloneError。transfer list / MessagePort 转移在后续阶段实现。
//
// Hole 只出现在数组元素位置，用于保留稀疏数组的空洞（还原时跳过该索引）。
//
// 关联 ADR: 010（接口-实现-工厂）、028（infra）

#ifndef NEO_WORKER_MESSAGEVALUE_H
#define NEO_WORKER_MESSAGEVALUE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neo::worker {

class MessagePortEndpoint;

enum class MessageValueKind : uint8_t {
    Undefined,
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
    Hole,  // 仅用于数组元素位置
    Date,
    Map,
    Set,
    RegExp,
    ArrayBuffer,
    DataView,
    TypedArray,
    MessagePort,  // 转移的 MessagePort 端点
    Reference,    // 指向同一消息中 id 为 ref 的已克隆对象（循环/共享引用）
};

struct MessageValue {
    MessageValueKind kind = MessageValueKind::Undefined;
    // 对象图节点 id（仅对象类节点非 0）。Reference 用 ref 指向目标 id。
    uint32_t id = 0;
    uint32_t ref = 0;

    bool boolean = false;
    double number = 0.0;
    std::string string;   // String 值 / RegExp source
    std::string string2;  // RegExp flags

    std::vector<MessageValue> array;                              // Array
    std::vector<std::pair<std::string, MessageValue>> object;     // Object
    std::vector<std::pair<MessageValue, MessageValue>> entries;   // Map
    std::vector<MessageValue> items;                              // Set
    std::vector<uint8_t> bytes;                                   // ArrayBuffer

    // DataView / TypedArray：buffer 恰好含一个 ArrayBuffer 节点。
    std::vector<MessageValue> buffer;
    size_t byteOffset = 0;
    size_t byteLength = 0;    // DataView
    size_t length = 0;        // TypedArray 元素个数
    uint8_t typedArrayKind = 0;

    // MessagePort 转移端点（与 Context 无关的共享句柄）。
    std::shared_ptr<MessagePortEndpoint> port;

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
    static MessageValue reference(uint32_t id) {
        MessageValue v;
        v.kind = MessageValueKind::Reference;
        v.ref = id;
        return v;
    }
};

}  // namespace neo::worker

#endif  // NEO_WORKER_MESSAGEVALUE_H
