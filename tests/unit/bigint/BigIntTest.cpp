// BigIntTest.cpp - 纯 C++ BigInt 类
//
// 覆盖：构造/进制解析与输出、四则运算、Knuth 长除法、位运算（补码语义）、
// 移位、比较、64 位截断转换、pow。

#include "bigint/BigInt.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using neo::bigint::BigInt;

BigInt parse10(const std::string& s) {
    BigInt out;
    EXPECT_TRUE(BigInt::parse(s, 10, out)) << s;
    return out;
}

std::string int128ToString(__int128 value) {
    if (value == 0) return "0";
    const bool negative = value < 0;
    unsigned __int128 u = negative
                              ? -static_cast<unsigned __int128>(value)
                              : static_cast<unsigned __int128>(value);
    std::string s;
    while (u != 0) {
        s.push_back(static_cast<char>('0' + static_cast<int>(u % 10)));
        u /= 10;
    }
    if (negative) s.push_back('-');
    std::reverse(s.begin(), s.end());
    return s;
}

uint64_t nextRandom(uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

}  // namespace

TEST(BigIntTest, ConstructAndToString) {
    EXPECT_EQ(BigInt().toString(), "0");
    EXPECT_EQ(BigInt(0).toString(), "0");
    EXPECT_EQ(BigInt(1).toString(), "1");
    EXPECT_EQ(BigInt(-1).toString(), "-1");
    EXPECT_EQ(BigInt(static_cast<int64_t>(-123456789)).toString(), "-123456789");
    EXPECT_EQ(BigInt(std::numeric_limits<int64_t>::min()).toString(),
              "-9223372036854775808");
    EXPECT_EQ(BigInt(std::numeric_limits<int64_t>::max()).toString(),
              "9223372036854775807");
    EXPECT_EQ(BigInt(std::numeric_limits<uint64_t>::max()).toString(),
              "18446744073709551615");
}

TEST(BigIntTest, ParseAndRadixOutput) {
    EXPECT_EQ(parse10("0").toString(), "0");
    EXPECT_EQ(parse10("-0").toString(), "0");
    EXPECT_EQ(parse10("+42").toString(), "42");
    EXPECT_EQ(parse10("00123").toString(), "123");
    EXPECT_EQ(parse10("-123456789012345678901234567890").toString(),
              "-123456789012345678901234567890");

    BigInt hex;
    ASSERT_TRUE(BigInt::parse("ff", 16, hex));
    EXPECT_EQ(hex.toString(), "255");
    EXPECT_EQ(hex.toString(16), "ff");

    BigInt base36;
    ASSERT_TRUE(BigInt::parse("z", 36, base36));
    EXPECT_EQ(base36.toString(), "35");
    EXPECT_EQ(base36.toString(36), "z");

    // 多进制往返。
    BigInt n = parse10("123456789012345678901234567890");
    for (int radix : {2, 8, 16, 36}) {
        BigInt back;
        ASSERT_TRUE(BigInt::parse(n.toString(radix), radix, back)) << radix;
        EXPECT_EQ(back, n) << radix;
    }
}

TEST(BigIntTest, ParseRejectsInvalid) {
    BigInt out;
    EXPECT_FALSE(BigInt::parse("", 10, out));
    EXPECT_FALSE(BigInt::parse("-", 10, out));
    EXPECT_FALSE(BigInt::parse("+", 10, out));
    EXPECT_FALSE(BigInt::parse("12a", 10, out));
    EXPECT_FALSE(BigInt::parse("2", 2, out));
    EXPECT_FALSE(BigInt::parse("1 ", 10, out));
    EXPECT_FALSE(BigInt::parse("1", 1, out));
    EXPECT_FALSE(BigInt::parse("1", 37, out));
}

TEST(BigIntTest, AdditionAndSubtraction) {
    EXPECT_EQ((BigInt(1) + BigInt(2)).toString(), "3");
    EXPECT_EQ((BigInt(-5) + BigInt(3)).toString(), "-2");
    EXPECT_EQ((BigInt(5) + BigInt(-3)).toString(), "2");
    EXPECT_EQ((BigInt(-5) + BigInt(-3)).toString(), "-8");
    EXPECT_EQ((BigInt(5) - BigInt(8)).toString(), "-3");
    EXPECT_EQ((BigInt(-5) - BigInt(-8)).toString(), "3");

    // 跨 limb 进位。
    BigInt max32(static_cast<uint64_t>(0xFFFFFFFFu));
    EXPECT_EQ((max32 + BigInt(1)).toString(), "4294967296");
    BigInt max64(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ((max64 + BigInt(1)).toString(), "18446744073709551616");
    EXPECT_EQ((max64 + BigInt(1) - BigInt(1)), max64);
}

TEST(BigIntTest, Multiplication) {
    EXPECT_EQ((BigInt(123456789) * BigInt(987654321)).toString(),
              "121932631112635269");
    EXPECT_EQ((BigInt(-7) * BigInt(6)).toString(), "-42");
    EXPECT_EQ((BigInt(-7) * BigInt(-6)).toString(), "42");

    BigInt twoPow64 = BigInt(1) << 64;
    BigInt max64(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ((max64 * max64).toString(),
              "340282366920938463426481119284349108225");
    EXPECT_EQ((twoPow64 * twoPow64).toString(),
              "340282366920938463463374607431768211456");

    // 25!
    BigInt fact(1);
    for (int i = 2; i <= 25; ++i) fact = fact * BigInt(i);
    EXPECT_EQ(fact.toString(), "15511210043330985984000000");
}

TEST(BigIntTest, DivisionAndModulo) {
    BigInt q;
    BigInt r;
    ASSERT_TRUE(BigInt::divMod(parse10("100"), BigInt(7), q, r));
    EXPECT_EQ(q.toString(), "14");
    EXPECT_EQ(r.toString(), "2");

    // 10^30 = 7 * 142857142857142857142857142857 + 1
    BigInt n = parse10("1000000000000000000000000000000");
    ASSERT_TRUE(BigInt::divMod(n, BigInt(7), q, r));
    EXPECT_EQ(q.toString(), "142857142857142857142857142857");
    EXPECT_EQ(r.toString(), "1");

    // 大除数走 Knuth D：q*d + r == n。
    BigInt d = (BigInt(1) << 50) + BigInt(1);
    ASSERT_TRUE(BigInt::divMod(n, d, q, r));
    EXPECT_EQ(q * d + r, n);
    EXPECT_TRUE(r < d);

    // 符号：向零截断，余数跟随被除数。
    EXPECT_EQ((BigInt(-7) / BigInt(2)).toString(), "-3");
    EXPECT_EQ((BigInt(-7) % BigInt(2)).toString(), "-1");
    EXPECT_EQ((BigInt(7) / BigInt(-2)).toString(), "-3");
    EXPECT_EQ((BigInt(7) % BigInt(-2)).toString(), "1");
    EXPECT_EQ((BigInt(-7) / BigInt(-2)).toString(), "3");
    EXPECT_EQ((BigInt(-7) % BigInt(-2)).toString(), "-1");

    // 除数为零。
    EXPECT_FALSE(BigInt::divMod(BigInt(1), BigInt(0), q, r));
    EXPECT_FALSE(BigInt::divMod(BigInt(0), BigInt(0), q, r));
    ASSERT_TRUE(BigInt::divMod(BigInt(0), BigInt(5), q, r));
    EXPECT_TRUE(q.isZero());
    EXPECT_TRUE(r.isZero());
}

TEST(BigIntTest, BitwiseTwosComplement) {
    EXPECT_EQ((BigInt(0xF0) & BigInt(0x0F)).toString(), "0");
    EXPECT_EQ((BigInt(0xF0) | BigInt(0x0F)).toString(), "255");
    EXPECT_EQ((BigInt(0xFF) ^ BigInt(0x0F)).toString(), "240");
    EXPECT_EQ((~BigInt(0)).toString(), "-1");
    EXPECT_EQ((~BigInt(5)).toString(), "-6");

    EXPECT_EQ((BigInt(-1) & BigInt(5)).toString(), "5");
    EXPECT_EQ((BigInt(-2) & BigInt(3)).toString(), "2");
    EXPECT_EQ((BigInt(-1) | BigInt(5)).toString(), "-1");
    EXPECT_EQ((BigInt(-5) | BigInt(3)).toString(), "-5");
    EXPECT_EQ((BigInt(-5) ^ BigInt(3)).toString(), "-8");

    BigInt p = BigInt(1) << 100;
    BigInt mask = p - BigInt(1);
    EXPECT_TRUE(((-p) & mask).isZero());
    EXPECT_EQ(((-p) | mask).toString(), "-1");
}

TEST(BigIntTest, Shifts) {
    EXPECT_EQ((BigInt(1) << 100).toString(),
              "1267650600228229401496703205376");
    EXPECT_EQ(((BigInt(1) << 100) >> 100).toString(), "1");
    EXPECT_EQ((BigInt(5) >> 1).toString(), "2");
    EXPECT_EQ((BigInt(-1) >> 1).toString(), "-1");
    EXPECT_EQ((BigInt(-1) >> 100).toString(), "-1");
    EXPECT_EQ((BigInt(-5) >> 1).toString(), "-3");
    EXPECT_EQ((BigInt(-4) >> 2).toString(), "-1");
    EXPECT_EQ((BigInt(-5) << 2).toString(), "-20");
    EXPECT_EQ((BigInt(0) << 1000).toString(), "0");
}

TEST(BigIntTest, Comparison) {
    EXPECT_TRUE(BigInt(-2) < BigInt(-1));
    EXPECT_TRUE(BigInt(-1) < BigInt(0));
    EXPECT_TRUE(BigInt(0) < BigInt(1));
    EXPECT_TRUE(BigInt(1) < (BigInt(1) << 64));
    EXPECT_EQ(BigInt(42).compare(BigInt(42)), 0);
    EXPECT_EQ(BigInt(-42).compare(BigInt(-42)), 0);
    EXPECT_TRUE(BigInt(-42) != BigInt(42));
}

TEST(BigIntTest, Pow) {
    EXPECT_EQ(BigInt(2).pow(10).toString(), "1024");
    EXPECT_EQ(BigInt(-2).pow(3).toString(), "-8");
    EXPECT_EQ(BigInt(-2).pow(4).toString(), "16");
    EXPECT_EQ(BigInt(0).pow(0).toString(), "1");
    EXPECT_EQ(BigInt(0).pow(5).toString(), "0");
    EXPECT_EQ(BigInt(10).pow(30).toString(),
              "1000000000000000000000000000000");
}

TEST(BigIntTest, BitLengthAndConversions) {
    EXPECT_EQ(BigInt(0).bitLength(), 0u);
    EXPECT_EQ(BigInt(1).bitLength(), 1u);
    EXPECT_EQ(BigInt(255).bitLength(), 8u);
    EXPECT_EQ(BigInt(256).bitLength(), 9u);
    EXPECT_EQ(BigInt(-256).bitLength(), 9u);

    EXPECT_EQ(BigInt(-1).toUint64(), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(BigInt(-1).toInt64(), -1);
    EXPECT_EQ(BigInt(std::numeric_limits<uint64_t>::max()).toUint64(),
              std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(BigInt(static_cast<int64_t>(-2)).toUint64(),
              std::numeric_limits<uint64_t>::max() - 1);

    EXPECT_TRUE(BigInt(-1).fitsInt64());
    EXPECT_TRUE(BigInt(std::numeric_limits<int64_t>::min()).fitsInt64());
    EXPECT_FALSE((BigInt(1) << 63).fitsInt64());
    EXPECT_TRUE(BigInt(std::numeric_limits<uint64_t>::max()).fitsUint64());
    EXPECT_FALSE(BigInt(-1).fitsUint64());
    EXPECT_FALSE((BigInt(1) << 64).fitsUint64());
}

TEST(BigIntTest, RandomizedAgainstInt128) {
    uint64_t state = 0x123456789ABCDEF0ULL;
    for (int iter = 0; iter < 5000; ++iter) {
        int64_t a = static_cast<int64_t>(nextRandom(state));
        int64_t b = static_cast<int64_t>(nextRandom(state));
        __int128 ea = static_cast<__int128>(a);
        __int128 eb = static_cast<__int128>(b);
        SCOPED_TRACE(::testing::Message() << "a=" << a << " b=" << b);

        EXPECT_EQ((BigInt(a) + BigInt(b)).toString(), int128ToString(ea + eb));
        EXPECT_EQ((BigInt(a) - BigInt(b)).toString(), int128ToString(ea - eb));
        EXPECT_EQ((BigInt(a) * BigInt(b)).toString(), int128ToString(ea * eb));
        EXPECT_EQ((BigInt(a) & BigInt(b)).toString(),
                  int128ToString(ea & eb));
        EXPECT_EQ((BigInt(a) | BigInt(b)).toString(),
                  int128ToString(ea | eb));
        EXPECT_EQ((BigInt(a) ^ BigInt(b)).toString(),
                  int128ToString(ea ^ eb));
    }
}

// 32 位操作数让期望值落在 int64 内，避免依赖 __int128 除法运行时。
TEST(BigIntTest, RandomizedDivisionAgainstInt64) {
    uint64_t state = 0x0F1E2D3C4B5A6978ULL;
    for (int iter = 0; iter < 5000; ++iter) {
        int64_t a = static_cast<int32_t>(nextRandom(state));
        int64_t b = static_cast<int32_t>(nextRandom(state));
        if (b == 0) continue;
        SCOPED_TRACE(::testing::Message() << "a=" << a << " b=" << b);

        BigInt q;
        BigInt r;
        ASSERT_TRUE(BigInt::divMod(BigInt(a), BigInt(b), q, r));
        EXPECT_EQ(q.toString(), std::to_string(a / b));
        EXPECT_EQ(r.toString(), std::to_string(a % b));
    }
}

TEST(BigIntTest, RandomizedLargeDivisionInvariant) {
    uint64_t state = 0xDEADBEEFCAFEBABEULL;
    for (int iter = 0; iter < 500; ++iter) {
        BigInt n;
        BigInt d;
        int nLimbs = 1 + static_cast<int>(nextRandom(state) % 8);
        int dLimbs = 1 + static_cast<int>(nextRandom(state) % 5);
        for (int i = 0; i < nLimbs; ++i) {
            n = (n << 32) |
                BigInt(static_cast<uint64_t>(nextRandom(state) & 0xFFFFFFFFu));
        }
        for (int i = 0; i < dLimbs; ++i) {
            d = (d << 32) |
                BigInt(static_cast<uint64_t>(nextRandom(state) & 0xFFFFFFFFu));
        }
        if (d.isZero()) d = BigInt(1);
        if (nextRandom(state) & 1) n = -n;
        if (nextRandom(state) & 1) d = -d;

        BigInt q;
        BigInt r;
        ASSERT_TRUE(BigInt::divMod(n, d, q, r));
        SCOPED_TRACE(::testing::Message() << "n=" << n.toString()
                                          << " d=" << d.toString());
        EXPECT_EQ(q * d + r, n);
        EXPECT_TRUE(r.abs() < d.abs());
    }
}
