// SharedBuffer.h - 跨 isolate/线程共享的字节块
//
// SharedArrayBuffer 的底层内存不属于任何 Context 的 GC，也不计入 worker 子分配器：
// 共享块的存活跨越线程，无法归属到单一配额。
//
// 生命周期由 shared_ptr 引用计数管理：每个 SharedArrayBuffer 包装对象持有一个
// 引用，结构化克隆时 MessageValue 再持有一个，最后一个引用释放时内存归还宿主堆。
// 引用计数由 shared_ptr 的控制块原子维护，不需要手写 retain/release。
//
// 关联 ADR: 015（data 持子 value）、028（infra）

#ifndef NEO_CORE_SHAREDBUFFER_H
#define NEO_CORE_SHAREDBUFFER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace neo::core {

class SharedBufferBlock {
public:
    // 分配失败抛 std::bad_alloc，由调用方转换为 OOM 哨兵。
    static std::shared_ptr<SharedBufferBlock> create(size_t byteLength) {
        return std::shared_ptr<SharedBufferBlock>(
            new SharedBufferBlock(byteLength));
    }

    uint8_t* data() { return bytes_.data(); }
    const uint8_t* data() const { return bytes_.data(); }
    size_t byteLength() const { return bytes_.size(); }

private:
    explicit SharedBufferBlock(size_t byteLength) : bytes_(byteLength) {}

    std::vector<uint8_t> bytes_;
};

}  // namespace neo::core

#endif  // NEO_CORE_SHAREDBUFFER_H
