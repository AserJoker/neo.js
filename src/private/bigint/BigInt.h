// bigint/BigInt.h - 纯 C++ 任意精度有符号整数
//
// 与引擎无关：不依赖 Value/IContext/GC，只做数学运算与进制转换。
// 引擎层（TypeKind::BigInt）后续在此之上做包装、错误映射与内建方法。
//
// 表示：
//   - magnitude 为 little-endian 的 uint32 limb 数组，无前导零 limb；
//   - 零表示为空数组且 negative_ == false（不存在 -0）；
//   - 符号独立于 magnitude，所有运算先算 magnitude 再定符号。
//
// 运算语义对齐 ECMAScript BigInt：
//   - / 与 % 向零截断，余数符号跟随被除数；
//   - 位运算按无限符号扩展的二进制补码语义；
//   - >> 为算术右移（负数向下取整）。
//
// 关联 ADR: 050（BigInt）

#ifndef NEO_BIGINT_BIGINT_H
#define NEO_BIGINT_BIGINT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo::bigint {

class BigInt {
public:
    BigInt() = default;
    BigInt(int value) : BigInt(static_cast<int64_t>(value)) {}
    BigInt(int64_t value);           // NOLINT(google-explicit-constructor)
    explicit BigInt(uint64_t value);

    // 解析 text（radix 2..36，允许前导 '+'/'-'，至少一位数字）。
    // 成功写入 out 并返回 true；空串/非法字符/非法 radix 返回 false。
    static bool parse(std::string_view text, int radix, BigInt& out);

    bool isZero() const { return mag_.empty(); }
    bool isNegative() const { return negative_; }
    size_t bitLength() const;

    // radix 2..36；radix 非法返回空串。负数带 '-' 前缀，数字 10..35 用小写字母。
    std::string toString(int radix = 10) const;

    // 比较：-1 / 0 / 1。
    int compare(const BigInt& other) const;
    bool operator==(const BigInt& other) const { return compare(other) == 0; }
    bool operator!=(const BigInt& other) const { return compare(other) != 0; }
    bool operator<(const BigInt& other) const { return compare(other) < 0; }
    bool operator<=(const BigInt& other) const { return compare(other) <= 0; }
    bool operator>(const BigInt& other) const { return compare(other) > 0; }
    bool operator>=(const BigInt& other) const { return compare(other) >= 0; }

    BigInt operator-() const;
    BigInt abs() const;

    BigInt operator+(const BigInt& other) const;
    BigInt operator-(const BigInt& other) const;
    BigInt operator*(const BigInt& other) const;

    // 除数为零时返回 false，不修改 q/r（引擎层负责先抛 RangeError）。
    static bool divMod(const BigInt& dividend, const BigInt& divisor,
                       BigInt& quotient, BigInt& remainder);
    // 前置条件：除数非零。
    BigInt operator/(const BigInt& other) const;
    BigInt operator%(const BigInt& other) const;

    // exponent 为非负整数（负指数由引擎层抛 RangeError）。
    BigInt pow(uint64_t exponent) const;

    // 位运算：无限符号扩展的二进制补码语义。
    BigInt operator&(const BigInt& other) const;
    BigInt operator|(const BigInt& other) const;
    BigInt operator^(const BigInt& other) const;
    BigInt operator~() const;
    BigInt operator<<(uint64_t bits) const;
    BigInt operator>>(uint64_t bits) const;  // 算术右移

    // 低 64 位的补码值（BigInt.asUintN/asIntN 的截断语义）。
    uint64_t toUint64() const;
    int64_t toInt64() const;
    bool fitsUint64() const;
    bool fitsInt64() const;

private:
    BigInt(bool negative, std::vector<uint32_t> magnitude)
        : negative_(negative), mag_(std::move(magnitude)) {
        trim();
    }

    void trim();

    bool negative_ = false;
    std::vector<uint32_t> mag_;  // little-endian，无前导零
};

}  // namespace neo::bigint

#endif  // NEO_BIGINT_BIGINT_H
