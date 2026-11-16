// RegexHost.h - 正则引擎的宿主环境（ADR-062）
//
// Host 只在两个位置出现：
//   1. 编译期：提供 Unicode 属性集 / case folding 表（R3 起使用）；
//   2. 执行期：每 N 条指令检查一次 shouldAbort()（步数预算 / 中断）。
//
// 匹配热路径不做 Host 虚调用：字符类、\p{...}、大小写折叠在编译期展开。
//
// 关联 ADR: 062（正则引擎）

#ifndef NEO_REGEX_HOST_H
#define NEO_REGEX_HOST_H

#include <cstdint>

namespace neo::regex {

// Unicode 数据提供者。R0 仅作为占位接口，R3 扩展属性/折叠查询。
class UnicodeTables {
public:
    virtual ~UnicodeTables() = default;
};

class RegexHost {
public:
    virtual ~RegexHost() = default;

    // 编译期使用；返回的引用生命周期由宿主保证。
    virtual const UnicodeTables& unicode() const = 0;

    // 执行期每 N 条指令调用一次；返回 true 表示中止本次匹配。
    virtual bool shouldAbort(std::uint64_t steps) = 0;
};

}  // namespace neo::regex

#endif  // NEO_REGEX_HOST_H
