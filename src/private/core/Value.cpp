// value.cpp - Value 构造/析构实现
//
// Value 构造调 data->ref++，析构调 data->ref--（不销毁 data，data 由 GC sweep 释放）。
//
// 关联 ADR: 015（Value meta+data 引用计数分层）、029（生命周期）

#include "core/Value.h"

namespace neo::core {

Value::Value(IType* type, IData* data)
    : type_(type), data_(data) {
    if (data_ != nullptr) {
        ++data_->ref_;
    }
}

Value::~Value() {
    // 仅递减 data 引用计数，不释放 data（GC sweep 在 ref 归 0 时 allocator->free）
    // 注：data_ 的最终释放由 GC 负责（GC 持 IAllocator*）
    if (data_ != nullptr) {
        --data_->ref_;
    }
}

IData* Value::replaceMeta(IType* type, IData* data) {
    // 旧 data ref--（与析构同理，不释放），返回给调用方决定是否回收
    IData* old = data_;
    if (old != nullptr) {
        --old->ref_;
    }
    type_ = type;
    data_ = data;
    // 新 data ref++（与构造同理）
    if (data_ != nullptr) {
        ++data_->ref_;
    }
    return old;
}

}  // namespace neo::core
