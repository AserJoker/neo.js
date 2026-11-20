// Test262FrontmatterTest.cpp - Test262 元数据解析单测

#include "Frontmatter.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using neo::test262::Metadata;
using neo::test262::parseMetadata;

TEST(Test262FrontmatterTest, ParsesInlineListsAndNegative) {
    const std::string source =
        "// header\n"
        "/*---\n"
        "description: >\n"
        "  multi line description\n"
        "includes: [assert.js, propertyHelper.js]\n"
        "flags: [onlyStrict]\n"
        "features: [BigInt, Symbol]\n"
        "negative:\n"
        "  phase: runtime\n"
        "  type: TypeError\n"
        "info: |\n"
        "  phase: this must be ignored\n"
        "---*/\n"
        "throw new TypeError();\n";

    const Metadata meta = parseMetadata(source);
    ASSERT_EQ(meta.includes.size(), 2U);
    EXPECT_EQ(meta.includes[0], "assert.js");
    EXPECT_EQ(meta.includes[1], "propertyHelper.js");
    ASSERT_EQ(meta.flags.size(), 1U);
    EXPECT_TRUE(meta.hasFlag("onlyStrict"));
    ASSERT_EQ(meta.features.size(), 2U);
    EXPECT_TRUE(meta.hasFeature("BigInt"));
    EXPECT_TRUE(meta.hasNegative);
    EXPECT_EQ(meta.negative.phase, "runtime");
    EXPECT_EQ(meta.negative.type, "TypeError");
}

TEST(Test262FrontmatterTest, ParsesBlockLists) {
    const std::string source =
        "/*---\n"
        "includes:\n"
        "  - sta.js\n"
        "  - compareArray.js\n"
        "flags:\n"
        "  - noStrict\n"
        "features:\n"
        "  - Atomics\n"
        "---*/\n";

    const Metadata meta = parseMetadata(source);
    ASSERT_EQ(meta.includes.size(), 2U);
    EXPECT_EQ(meta.includes[0], "sta.js");
    EXPECT_EQ(meta.includes[1], "compareArray.js");
    ASSERT_EQ(meta.flags.size(), 1U);
    EXPECT_TRUE(meta.hasFlag("noStrict"));
    ASSERT_EQ(meta.features.size(), 1U);
    EXPECT_TRUE(meta.hasFeature("Atomics"));
    EXPECT_FALSE(meta.hasNegative);
}

TEST(Test262FrontmatterTest, MissingFrontmatterIsEmpty) {
    const Metadata meta = parseMetadata("var x = 1;\n");
    EXPECT_TRUE(meta.includes.empty());
    EXPECT_TRUE(meta.flags.empty());
    EXPECT_TRUE(meta.features.empty());
    EXPECT_FALSE(meta.hasNegative);
}

}  // namespace
