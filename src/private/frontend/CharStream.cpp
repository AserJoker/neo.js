// frontend/CharStream.cpp - UTF-8 字符流实现

#include "frontend/CharStream.h"

#include "unicode/UnicodeClassification.h"
#include "unicode/Utf8.h"

namespace neo::frontend {

uint32_t CharStream::peek(size_t lookahead) const {
    size_t offset = offset_;
    for (size_t index = 0; index <= lookahead; ++index) {
        if (offset >= source_.size()) return kEndOfInput;
        uint32_t cp = 0;
        size_t next = offset;
        if (!unicode::decodeCodePoint(source_, next, cp)) {
            if (index == lookahead) return 0xFFFDu;
            ++offset;  // 非法字节按一个字节跳过
            continue;
        }
        if (index == lookahead) return cp;
        offset = next;
    }
    return kEndOfInput;
}

uint32_t CharStream::advance() {
    if (offset_ >= source_.size()) return kEndOfInput;

    uint32_t cp = 0;
    size_t next = offset_;
    if (!unicode::decodeCodePoint(source_, next, cp)) {
        invalidUtf8_ = true;
        cp = 0xFFFDu;
        next = offset_ + 1;
    }
    offset_ = next;

    if (cp == '\r') {
        ++line_;
        column_ = 1;
        lastWasCR_ = true;
    } else if (cp == '\n') {
        if (!lastWasCR_) ++line_;
        column_ = 1;
        lastWasCR_ = false;
    } else if (unicode::isLineTerminator(cp)) {
        // LS / PS（LF/CR 已在上面的分支处理）。
        ++line_;
        column_ = 1;
        lastWasCR_ = false;
    } else {
        ++column_;
        lastWasCR_ = false;
    }
    return cp;
}

}  // namespace neo::frontend
