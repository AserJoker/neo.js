// Frontmatter.h - Test262 元数据（/*--- ... ---*/）解析
//
// Test262 的 YAML 元数据只用到很小的子集：标量、行内数组、块列表、
// negative 字典。这里按行解析，不引入 YAML 依赖。
//
// 关联 ADR: 027（测试规范）

#ifndef NEO_TOOLS_TEST262_FRONTMATTER_H
#define NEO_TOOLS_TEST262_FRONTMATTER_H

#include <string>
#include <string_view>
#include <vector>

namespace neo::test262 {

struct NegativeInfo {
    std::string phase;
    std::string type;
};

struct Metadata {
    std::vector<std::string> includes;
    std::vector<std::string> flags;
    std::vector<std::string> features;
    NegativeInfo negative;
    bool hasNegative = false;

    bool hasFlag(std::string_view flag) const;
    bool hasFeature(std::string_view feature) const;
};

// 解析源文件中的第一段 /*--- ... ---*/；不存在时返回空元数据。
Metadata parseMetadata(std::string_view source);

}  // namespace neo::test262

#endif  // NEO_TOOLS_TEST262_FRONTMATTER_H
