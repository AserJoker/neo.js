// RegexSubject.cpp - Subject 视图（ADR-062）

#include "regex/RegexSubject.h"

namespace neo::regex {

Subject Subject::fromAscii(std::string_view bytes) {
    Subject subject;
    subject.isAscii_ = true;
    subject.ascii_ = bytes;
    return subject;
}

Subject Subject::fromUtf16(std::u16string_view units) {
    Subject subject;
    subject.isAscii_ = false;
    subject.utf16_ = units;
    return subject;
}

std::uint32_t Subject::codePointAt(std::size_t i, std::size_t& width) const {
    if (isAscii_) {
        width = 1;
        return static_cast<unsigned char>(ascii_[i]);
    }
    const char16_t first = utf16_[i];
    if (first >= 0xD800 && first <= 0xDBFF && i + 1 < utf16_.size()) {
        const char16_t second = utf16_[i + 1];
        if (second >= 0xDC00 && second <= 0xDFFF) {
            width = 2;
            return 0x10000u +
                   ((static_cast<std::uint32_t>(first) - 0xD800u) << 10) +
                   (static_cast<std::uint32_t>(second) - 0xDC00u);
        }
    }
    width = 1;
    return static_cast<std::uint32_t>(first);
}

}  // namespace neo::regex
