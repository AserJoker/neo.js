// SourceText.h - 源码文本单例与函数切片（ADR-047 §6）
//
// module/eval 各自持有一份 SourceText；函数只保存 SourceSlice（source + offset +
// length），不复制源码字符串。Function.prototype.toString 调用时按切片生成 JS string。
//
// 生命周期：SourceText 引用计数。创建时 refCount=1（持有者）；函数 setFunctionSource
// 时 retain，FunctionData 析构时 release。最后一个引用释放时 delete。
// SourceText 是宿主堆上的不可变字节缓冲，不是 GC 托管的 JS Value。

#ifndef NEO_CORE_SOURCETEXT_H
#define NEO_CORE_SOURCETEXT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace neo::core {

struct SourceText {
    std::string text;        // 不可变源码
    size_t refCount = 0;     // 引用计数（单线程引擎）
};

// 创建一个 SourceText（refCount=1，调用方持有）。
inline SourceText* createSourceText(std::string text) {
    auto* s = new SourceText;
    s->text = std::move(text);
    s->refCount = 1;
    return s;
}

inline void retainSourceText(SourceText* s) {
    if (s != nullptr) ++s->refCount;
}

inline void releaseSourceText(SourceText* s) {
    if (s != nullptr && --s->refCount == 0) delete s;
}

// 函数持有的源码切片。source=nullptr 表示无源码（native 函数 / release 裁剪）。
struct SourceSlice {
    SourceText* source = nullptr;
    uint32_t offset = 0;
    uint32_t length = 0;
};

}  // namespace neo::core

#endif  // NEO_CORE_SOURCETEXT_H
