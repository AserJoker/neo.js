// WellKnownSymbol.h - well-known symbols 枚举（ES2024 §6.1.5.1）
//
// 每个 well-known symbol 在 Context 创建时生成一次，作为 Symbol 构造函数的
// 属性暴露（如 Symbol.iterator），并在 Context 生命周期内保持不变。
// 通过 IContext::getWellKnownSymbol(which) 获取对应的 Symbol Value。
//
// 关联 ADR: 016（Type 体系）、017（类型清单）

#ifndef NEO_CORE_WELL_KNOWN_SYMBOL_H
#define NEO_CORE_WELL_KNOWN_SYMBOL_H

#include <cstddef>
#include <cstdint>

namespace neo::core {

// 枚举顺序即 Context 内部数组下标，新增项只能追加到末尾。
enum class WellKnownSymbol : uint8_t {
    Iterator,           // Symbol.iterator
    AsyncIterator,      // Symbol.asyncIterator
    HasInstance,        // Symbol.hasInstance
    IsConcatSpreadable, // Symbol.isConcatSpreadable
    Match,              // Symbol.match
    MatchAll,           // Symbol.matchAll
    Replace,            // Symbol.replace
    Search,             // Symbol.search
    Species,            // Symbol.species
    Split,              // Symbol.split
    ToPrimitive,        // Symbol.toPrimitive
    ToStringTag,        // Symbol.toStringTag
    Unscopables,        // Symbol.unscopables
    Dispose,            // Symbol.dispose
    AsyncDispose,       // Symbol.asyncDispose
};

// well-known symbol 总数（Context 内部数组大小）
inline constexpr size_t kWellKnownSymbolCount = 15;

// 枚举值 → 属性名（Symbol 构造函数上的属性名，如 "iterator"）
inline const char* wellKnownSymbolName(WellKnownSymbol which) {
    switch (which) {
        case WellKnownSymbol::Iterator:           return "iterator";
        case WellKnownSymbol::AsyncIterator:      return "asyncIterator";
        case WellKnownSymbol::HasInstance:        return "hasInstance";
        case WellKnownSymbol::IsConcatSpreadable: return "isConcatSpreadable";
        case WellKnownSymbol::Match:              return "match";
        case WellKnownSymbol::MatchAll:           return "matchAll";
        case WellKnownSymbol::Replace:            return "replace";
        case WellKnownSymbol::Search:             return "search";
        case WellKnownSymbol::Species:            return "species";
        case WellKnownSymbol::Split:              return "split";
        case WellKnownSymbol::ToPrimitive:        return "toPrimitive";
        case WellKnownSymbol::ToStringTag:        return "toStringTag";
        case WellKnownSymbol::Unscopables:        return "unscopables";
        case WellKnownSymbol::Dispose:            return "dispose";
        case WellKnownSymbol::AsyncDispose:       return "asyncDispose";
    }
    return "";
}

}  // namespace neo::core

#endif  // NEO_CORE_WELL_KNOWN_SYMBOL_H
