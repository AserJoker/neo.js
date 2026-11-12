// bigint/BigInt.cpp - 任意精度整数实现
//
// magnitude 为 little-endian uint32 limb；乘法用 64 位中间量，除法用
// Knuth Algorithm D（n==1 走短除法）。位运算先转固定长度补码再逐 limb 计算。

#include "bigint/BigInt.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace neo::bigint {

namespace {

constexpr uint64_t kBase = 1ULL << 32;
constexpr uint32_t kLimbMask = 0xFFFFFFFFu;

uint32_t clz32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    while ((x & 0x80000000u) == 0) {
        x <<= 1;
        ++n;
    }
    return n;
}

void trimVector(std::vector<uint32_t>& v) {
    while (!v.empty() && v.back() == 0) v.pop_back();
}

int compareMagnitude(const std::vector<uint32_t>& a,
                     const std::vector<uint32_t>& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

std::vector<uint32_t> addMagnitude(const std::vector<uint32_t>& a,
                                   const std::vector<uint32_t>& b) {
    const bool aLarger = a.size() >= b.size();
    const std::vector<uint32_t>& big = aLarger ? a : b;
    const std::vector<uint32_t>& small = aLarger ? b : a;
    std::vector<uint32_t> out(big.size() + 1, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < big.size(); ++i) {
        uint64_t cur = static_cast<uint64_t>(big[i]) +
                       (i < small.size() ? small[i] : 0u) + carry;
        out[i] = static_cast<uint32_t>(cur);
        carry = cur >> 32;
    }
    out[big.size()] = static_cast<uint32_t>(carry);
    trimVector(out);
    return out;
}

// 前置条件：a >= b。
std::vector<uint32_t> subMagnitude(const std::vector<uint32_t>& a,
                                   const std::vector<uint32_t>& b) {
    std::vector<uint32_t> out(a.size(), 0);
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t cur = static_cast<int64_t>(a[i]) -
                      static_cast<int64_t>(i < b.size() ? b[i] : 0u) - borrow;
        if (cur < 0) {
            cur += static_cast<int64_t>(kBase);
            borrow = 1;
        } else {
            borrow = 0;
        }
        out[i] = static_cast<uint32_t>(cur);
    }
    trimVector(out);
    return out;
}

std::vector<uint32_t> mulMagnitude(const std::vector<uint32_t>& a,
                                   const std::vector<uint32_t>& b) {
    if (a.empty() || b.empty()) return {};
    std::vector<uint32_t> out(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.size(); ++j) {
            uint64_t cur = static_cast<uint64_t>(out[i + j]) +
                           static_cast<uint64_t>(a[i]) * b[j] + carry;
            out[i + j] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
        out[i + b.size()] = static_cast<uint32_t>(carry);
    }
    trimVector(out);
    return out;
}

std::vector<uint32_t> shiftLeftMagnitude(const std::vector<uint32_t>& v,
                                         uint64_t bits) {
    if (v.empty()) return {};
    size_t wordShift = static_cast<size_t>(bits / 32);
    uint32_t bitShift = static_cast<uint32_t>(bits % 32);
    std::vector<uint32_t> out(v.size() + wordShift + 1, 0);
    for (size_t i = 0; i < v.size(); ++i) {
        uint64_t cur = static_cast<uint64_t>(v[i]) << bitShift;
        out[i + wordShift] = static_cast<uint32_t>(cur);
        if (bitShift != 0) {
            out[i + wordShift + 1] |= static_cast<uint32_t>(cur >> 32);
        }
    }
    trimVector(out);
    return out;
}

std::vector<uint32_t> shiftRightMagnitude(const std::vector<uint32_t>& v,
                                          uint64_t bits) {
    size_t wordShift = static_cast<size_t>(bits / 32);
    uint32_t bitShift = static_cast<uint32_t>(bits % 32);
    if (wordShift >= v.size()) return {};
    std::vector<uint32_t> out(v.size() - wordShift, 0);
    for (size_t i = 0; i < out.size(); ++i) {
        uint64_t cur = static_cast<uint64_t>(v[i + wordShift]) >> bitShift;
        if (bitShift != 0 && i + wordShift + 1 < v.size()) {
            cur |= static_cast<uint64_t>(v[i + wordShift + 1]) << (32 - bitShift);
        }
        out[i] = static_cast<uint32_t>(cur);
    }
    trimVector(out);
    return out;
}

// 短除法：mag /= divisor，返回余数。
uint32_t divModSmall(std::vector<uint32_t>& mag, uint32_t divisor) {
    uint64_t rem = 0;
    for (size_t i = mag.size(); i-- > 0;) {
        uint64_t cur = (rem << 32) | mag[i];
        mag[i] = static_cast<uint32_t>(cur / divisor);
        rem = cur % divisor;
    }
    trimVector(mag);
    return static_cast<uint32_t>(rem);
}

// Knuth Algorithm D。前置条件：u、v 非空（v 非零）。
void divModMagnitude(const std::vector<uint32_t>& u,
                     const std::vector<uint32_t>& v,
                     std::vector<uint32_t>& q,
                     std::vector<uint32_t>& r) {
    q.clear();
    r.clear();
    if (compareMagnitude(u, v) < 0) {
        r = u;
        return;
    }

    const size_t n = v.size();
    if (n == 1) {
        q.assign(u.size(), 0);
        uint64_t rem = 0;
        for (size_t i = u.size(); i-- > 0;) {
            uint64_t cur = (rem << 32) | u[i];
            q[i] = static_cast<uint32_t>(cur / v[0]);
            rem = cur % v[0];
        }
        trimVector(q);
        if (rem != 0) r.push_back(static_cast<uint32_t>(rem));
        return;
    }

    const size_t m = u.size() - n;
    const uint32_t shift = clz32(v[n - 1]);

    // 归一化：使除数最高 limb 的最高位为 1。
    std::vector<uint32_t> vn(n, 0);
    for (size_t i = n; i-- > 0;) {
        uint64_t cur = static_cast<uint64_t>(v[i]) << shift;
        if (shift != 0 && i > 0) {
            cur |= static_cast<uint64_t>(v[i - 1]) >> (32 - shift);
        }
        vn[i] = static_cast<uint32_t>(cur);
    }
    std::vector<uint32_t> un(u.size() + 1, 0);
    for (size_t i = u.size(); i-- > 0;) {
        uint64_t cur = static_cast<uint64_t>(u[i]) << shift;
        if (shift != 0 && i > 0) {
            cur |= static_cast<uint64_t>(u[i - 1]) >> (32 - shift);
        }
        un[i] = static_cast<uint32_t>(cur);
    }
    if (shift != 0) {
        un[u.size()] = u[u.size() - 1] >> (32 - shift);
    }

    q.assign(m + 1, 0);
    for (size_t j = m + 1; j-- > 0;) {
        uint64_t top = (static_cast<uint64_t>(un[j + n]) << 32) | un[j + n - 1];
        uint64_t qhat = top / vn[n - 1];
        uint64_t rhat = top % vn[n - 1];
        while (qhat >= kBase ||
               qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            --qhat;
            rhat += vn[n - 1];
            if (rhat >= kBase) break;
        }

        // un[j..j+n] -= qhat * vn
        int64_t borrow = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t p = qhat * static_cast<uint64_t>(vn[i]);
            int64_t t = static_cast<int64_t>(un[i + j]) - borrow -
                        static_cast<int64_t>(p & kLimbMask);
            un[i + j] = static_cast<uint32_t>(t);
            borrow = static_cast<int64_t>(p >> 32) - (t >> 32);
        }
        int64_t t = static_cast<int64_t>(un[j + n]) - borrow;
        un[j + n] = static_cast<uint32_t>(t);

        if (t < 0) {
            // qhat 偏大 1，加回一次除数。
            --qhat;
            uint64_t carry = 0;
            for (size_t i = 0; i < n; ++i) {
                uint64_t sum = static_cast<uint64_t>(un[i + j]) + vn[i] + carry;
                un[i + j] = static_cast<uint32_t>(sum);
                carry = sum >> 32;
            }
            un[j + n] = static_cast<uint32_t>(
                static_cast<uint64_t>(un[j + n]) + carry);
        }
        q[j] = static_cast<uint32_t>(qhat);
    }
    trimVector(q);

    r.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        uint64_t cur = static_cast<uint64_t>(un[i]) >> shift;
        if (shift != 0) {
            cur |= static_cast<uint64_t>(un[i + 1]) << (32 - shift);
        }
        r[i] = static_cast<uint32_t>(cur);
    }
    trimVector(r);
}

// 固定长度二进制补码（little-endian）。
std::vector<uint32_t> toTwosComplement(bool negative,
                                       const std::vector<uint32_t>& mag,
                                       size_t length) {
    std::vector<uint32_t> out(length, 0);
    for (size_t i = 0; i < mag.size() && i < length; ++i) out[i] = mag[i];
    if (negative) {
        uint64_t carry = 1;
        for (size_t i = 0; i < length; ++i) {
            uint64_t cur = static_cast<uint64_t>(~out[i]) + carry;
            out[i] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
    }
    return out;
}

std::vector<uint32_t> fromTwosComplement(std::vector<uint32_t> twos,
                                         bool negative) {
    if (negative) {
        uint64_t carry = 1;
        for (size_t i = 0; i < twos.size(); ++i) {
            uint64_t cur = static_cast<uint64_t>(~twos[i]) + carry;
            twos[i] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
    }
    trimVector(twos);
    return twos;
}

char digitChar(uint32_t d) {
    return d < 10 ? static_cast<char>('0' + d)
                  : static_cast<char>('a' + (d - 10));
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

// mag = mag * mul + add。
void mulAddSmall(std::vector<uint32_t>& mag, uint32_t mul, uint32_t add) {
    uint64_t carry = add;
    for (size_t i = 0; i < mag.size(); ++i) {
        uint64_t cur = static_cast<uint64_t>(mag[i]) * mul + carry;
        mag[i] = static_cast<uint32_t>(cur);
        carry = cur >> 32;
    }
    while (carry != 0) {
        mag.push_back(static_cast<uint32_t>(carry));
        carry >>= 32;
    }
    trimVector(mag);
}

}  // namespace

BigInt::BigInt(int64_t value) {
    if (value < 0) {
        negative_ = true;
        // 避免对 INT64_MIN 直接取负。
        uint64_t magnitude = static_cast<uint64_t>(0) - static_cast<uint64_t>(value);
        mag_.push_back(static_cast<uint32_t>(magnitude));
        if ((magnitude >> 32) != 0) {
            mag_.push_back(static_cast<uint32_t>(magnitude >> 32));
        }
    } else {
        uint64_t magnitude = static_cast<uint64_t>(value);
        if (magnitude != 0) {
            mag_.push_back(static_cast<uint32_t>(magnitude));
            if ((magnitude >> 32) != 0) {
                mag_.push_back(static_cast<uint32_t>(magnitude >> 32));
            }
        }
    }
}

BigInt::BigInt(uint64_t value) {
    if (value != 0) {
        mag_.push_back(static_cast<uint32_t>(value));
        if ((value >> 32) != 0) {
            mag_.push_back(static_cast<uint32_t>(value >> 32));
        }
    }
}

void BigInt::trim() {
    trimVector(mag_);
    if (mag_.empty()) negative_ = false;
}

bool BigInt::parse(std::string_view text, int radix, BigInt& out) {
    if (radix < 2 || radix > 36 || text.empty()) return false;

    size_t pos = 0;
    bool negative = false;
    if (text[pos] == '+' || text[pos] == '-') {
        negative = text[pos] == '-';
        ++pos;
    }
    if (pos >= text.size()) return false;

    std::vector<uint32_t> mag;
    for (; pos < text.size(); ++pos) {
        int d = digitValue(text[pos]);
        if (d < 0 || d >= radix) return false;
        mulAddSmall(mag, static_cast<uint32_t>(radix), static_cast<uint32_t>(d));
    }

    out.negative_ = negative;
    out.mag_ = std::move(mag);
    out.trim();
    return true;
}

size_t BigInt::bitLength() const {
    if (mag_.empty()) return 0;
    return (mag_.size() - 1) * 32 + (32 - clz32(mag_.back()));
}

std::string BigInt::toString(int radix) const {
    if (radix < 2 || radix > 36) return {};
    if (mag_.empty()) return "0";

    uint64_t chunkBase = static_cast<uint64_t>(radix);
    int chunkDigits = 1;
    while (chunkBase * static_cast<uint64_t>(radix) <= kLimbMask) {
        chunkBase *= static_cast<uint64_t>(radix);
        ++chunkDigits;
    }

    std::vector<uint32_t> mag = mag_;
    std::vector<std::string> chunks;
    while (!mag.empty()) {
        uint32_t rem = divModSmall(mag, static_cast<uint32_t>(chunkBase));
        std::string part;
        uint32_t v = rem;
        for (int i = 0; i < chunkDigits; ++i) {
            part.push_back(digitChar(v % static_cast<uint32_t>(radix)));
            v /= static_cast<uint32_t>(radix);
        }
        std::reverse(part.begin(), part.end());
        chunks.push_back(std::move(part));
    }

    std::string out;
    if (negative_) out.push_back('-');
    // 最高位块去掉前导零。
    std::string& top = chunks.back();
    size_t first = top.find_first_not_of('0');
    out.append(top, first == std::string::npos ? top.size() : first,
               std::string::npos);
    for (size_t i = chunks.size() - 1; i-- > 0;) out += chunks[i];
    return out;
}

int BigInt::compare(const BigInt& other) const {
    if (negative_ != other.negative_) return negative_ ? -1 : 1;
    int cmp = compareMagnitude(mag_, other.mag_);
    return negative_ ? -cmp : cmp;
}

BigInt BigInt::operator-() const {
    BigInt out;
    out.negative_ = !negative_ && !mag_.empty();
    out.mag_ = mag_;
    return out;
}

BigInt BigInt::abs() const {
    BigInt out;
    out.mag_ = mag_;
    return out;
}

BigInt BigInt::operator+(const BigInt& other) const {
    if (negative_ == other.negative_) {
        return BigInt(negative_, addMagnitude(mag_, other.mag_));
    }
    int cmp = compareMagnitude(mag_, other.mag_);
    if (cmp == 0) return BigInt();
    if (cmp > 0) return BigInt(negative_, subMagnitude(mag_, other.mag_));
    return BigInt(other.negative_, subMagnitude(other.mag_, mag_));
}

BigInt BigInt::operator-(const BigInt& other) const {
    return *this + (-other);
}

BigInt BigInt::operator*(const BigInt& other) const {
    if (mag_.empty() || other.mag_.empty()) return BigInt();
    return BigInt(negative_ != other.negative_, mulMagnitude(mag_, other.mag_));
}

bool BigInt::divMod(const BigInt& dividend, const BigInt& divisor,
                    BigInt& quotient, BigInt& remainder) {
    if (divisor.mag_.empty()) return false;

    std::vector<uint32_t> q;
    std::vector<uint32_t> r;
    divModMagnitude(dividend.mag_, divisor.mag_, q, r);

    quotient = BigInt(dividend.negative_ != divisor.negative_, std::move(q));
    remainder = BigInt(dividend.negative_, std::move(r));
    return true;
}

BigInt BigInt::operator/(const BigInt& other) const {
    BigInt q;
    BigInt r;
    assert(!other.mag_.empty());
    divMod(*this, other, q, r);
    return q;
}

BigInt BigInt::operator%(const BigInt& other) const {
    BigInt q;
    BigInt r;
    assert(!other.mag_.empty());
    divMod(*this, other, q, r);
    return r;
}

BigInt BigInt::pow(uint64_t exponent) const {
    BigInt result(static_cast<uint64_t>(1));
    BigInt base = *this;
    while (exponent != 0) {
        if ((exponent & 1u) != 0) result = result * base;
        exponent >>= 1;
        if (exponent != 0) base = base * base;
    }
    return result;
}

BigInt BigInt::operator&(const BigInt& other) const {
    size_t len = std::max(mag_.size(), other.mag_.size()) + 1;
    auto a = toTwosComplement(negative_, mag_, len);
    auto b = toTwosComplement(other.negative_, other.mag_, len);
    for (size_t i = 0; i < len; ++i) a[i] &= b[i];
    return BigInt(negative_ && other.negative_, fromTwosComplement(std::move(a), negative_ && other.negative_));
}

BigInt BigInt::operator|(const BigInt& other) const {
    size_t len = std::max(mag_.size(), other.mag_.size()) + 1;
    auto a = toTwosComplement(negative_, mag_, len);
    auto b = toTwosComplement(other.negative_, other.mag_, len);
    for (size_t i = 0; i < len; ++i) a[i] |= b[i];
    bool neg = negative_ || other.negative_;
    return BigInt(neg, fromTwosComplement(std::move(a), neg));
}

BigInt BigInt::operator^(const BigInt& other) const {
    size_t len = std::max(mag_.size(), other.mag_.size()) + 1;
    auto a = toTwosComplement(negative_, mag_, len);
    auto b = toTwosComplement(other.negative_, other.mag_, len);
    for (size_t i = 0; i < len; ++i) a[i] ^= b[i];
    bool neg = negative_ != other.negative_;
    return BigInt(neg, fromTwosComplement(std::move(a), neg));
}

BigInt BigInt::operator~() const {
    return -(*this) - BigInt(static_cast<uint64_t>(1));
}

BigInt BigInt::operator<<(uint64_t bits) const {
    if (mag_.empty()) return BigInt();
    return BigInt(negative_, shiftLeftMagnitude(mag_, bits));
}

BigInt BigInt::operator>>(uint64_t bits) const {
    if (mag_.empty()) return BigInt();
    if (!negative_) return BigInt(false, shiftRightMagnitude(mag_, bits));

    // 算术右移 = 向下取整除以 2^bits。
    if (bits >= static_cast<uint64_t>(bitLength())) {
        return BigInt(static_cast<int64_t>(-1));
    }
    // (mag + 2^bits - 1) >> bits
    size_t wordShift = static_cast<size_t>(bits / 32);
    uint32_t bitShift = static_cast<uint32_t>(bits % 32);
    std::vector<uint32_t> addend(wordShift, 0xFFFFFFFFu);
    if (bitShift != 0) {
        addend.push_back((1u << bitShift) - 1u);
    }
    std::vector<uint32_t> adjusted = addMagnitude(mag_, addend);
    return BigInt(true, shiftRightMagnitude(adjusted, bits));
}

uint64_t BigInt::toUint64() const {
    uint64_t low = 0;
    if (!mag_.empty()) low = mag_[0];
    if (mag_.size() > 1) low |= static_cast<uint64_t>(mag_[1]) << 32;
    if (!negative_) return low;
    return ~low + 1;
}

int64_t BigInt::toInt64() const {
    return static_cast<int64_t>(toUint64());
}

bool BigInt::fitsUint64() const {
    return !negative_ && mag_.size() <= 2;
}

bool BigInt::fitsInt64() const {
    static const std::vector<uint32_t> kLimit = {0u, 0x80000000u};  // 2^63
    if (negative_) return compareMagnitude(mag_, kLimit) <= 0;
    return compareMagnitude(mag_, kLimit) < 0;
}

bool BigInt::fromDouble(double value, BigInt& out) {
    if (!std::isfinite(value) || std::trunc(value) != value) return false;
    if (value == 0.0) {
        out = BigInt();
        return true;
    }
    const bool negative = value < 0;
    const double absValue = std::fabs(value);
    int exponent = 0;
    const double mantissa = std::frexp(absValue, &exponent);  // abs = m * 2^e
    const uint64_t bits =
        static_cast<uint64_t>(std::ldexp(mantissa, 53));  // 53 位有效位
    const int shift = exponent - 53;
    BigInt magnitude(bits);
    if (shift >= 0) {
        magnitude = magnitude << static_cast<uint64_t>(shift);
    } else {
        magnitude = magnitude >> static_cast<uint64_t>(-shift);
    }
    out = negative ? -magnitude : magnitude;
    return true;
}

double BigInt::toDouble() const {
    if (mag_.empty()) return 0.0;

    const size_t bits = bitLength();
    double value = 0.0;
    if (bits <= 53) {
        uint64_t magnitude = mag_[0];
        if (mag_.size() > 1) {
            magnitude |= static_cast<uint64_t>(mag_[1]) << 32;
        }
        value = static_cast<double>(magnitude);
    } else {
        // 保留 54 位（53 位尾数 + 1 位 guard），再按 round-to-nearest-even 舍入。
        const size_t shift = bits - 53;
        const size_t topShift = shift - 1;
        const std::vector<uint32_t> topMag = shiftRightMagnitude(mag_, topShift);
        const uint64_t top = BigInt(false, topMag).toUint64();  // 54 位
        uint64_t mantissa = top >> 1;
        const bool guard = (top & 1u) != 0;
        const std::vector<uint32_t> lowMag = subMagnitude(
            mag_, shiftLeftMagnitude(topMag, topShift));
        const bool sticky = !lowMag.empty();
        if (guard && (sticky || (mantissa & 1u) != 0)) {
            ++mantissa;
            if (mantissa == (1ULL << 53)) {
                mantissa >>= 1;
                value = std::ldexp(static_cast<double>(mantissa),
                                   static_cast<int>(shift + 1));
                return negative_ ? -value : value;
            }
        }
        value = std::ldexp(static_cast<double>(mantissa),
                           static_cast<int>(shift));
    }
    return negative_ ? -value : value;
}

}  // namespace neo::bigint
