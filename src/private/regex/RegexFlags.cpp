// RegexFlags.cpp - flags 解析（ADR-062）

#include "regex/RegexFlags.h"

#include <cstddef>

namespace neo::regex {

FlagsResult parseFlags(std::string_view text) {
    FlagsResult result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        bool* slot = nullptr;
        switch (c) {
            case 'd': slot = &result.flags.hasIndices; break;
            case 'g': slot = &result.flags.global; break;
            case 'i': slot = &result.flags.ignoreCase; break;
            case 'm': slot = &result.flags.multiline; break;
            case 's': slot = &result.flags.dotAll; break;
            case 'u': slot = &result.flags.unicode; break;
            case 'v': slot = &result.flags.unicodeSets; break;
            case 'y': slot = &result.flags.sticky; break;
            default:
                result.error = CompileError{
                    i, std::string("Invalid regular expression flag '") + c +
                           "'"};
                return result;
        }
        if (*slot) {
            result.error = CompileError{
                i, std::string("Duplicate regular expression flag '") + c +
                       "'"};
            return result;
        }
        *slot = true;
    }
    if (result.flags.unicode && result.flags.unicodeSets) {
        result.error = CompileError{
            0, "Regular expression flags 'u' and 'v' are mutually exclusive"};
    }
    return result;
}

}  // namespace neo::regex
