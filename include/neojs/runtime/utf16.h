/*
 * 内部 C++ 实现：UTF-8/UTF-16 字符串视图工具。
 *
 * JS 字符串语义基于 UTF-16 code unit（ECMA-262 §6.1.4），而引擎内部
 * JSValue 以 UTF-8 字节存储（std::string）。String.prototype 方法
 * （length/charAt/charCodeAt/substring/slice/indexOf/...）全部按
 * UTF-16 code unit 索引操作，因此需要本模块在两种编码间建立映射。
 *
 * 设计：不整体转换字符串（避免每个方法 O(n) 复制），而是提供
 *   1. UTF-16 长度（code unit 数）
 *   2. UTF-16 索引 -> UTF-8 字节偏移（用于切片起点）
 *   3. UTF-16 索引处的 code unit 值（charCodeAt）
 *   4. 按 UTF-16 区间取子串（返回 UTF-8 串）
 *   5. 按 code unit 搜索（indexOf/lastIndexOf）
 *   6. trim 空白判断 + Unicode 大小写映射（ICU Default Case Conversion）
 *   7. 全串 UTF-8 <-> UTF-16 转换
 *
 * 遵循 coding-standards：#3 不抛 C++ 异常（纯函数，无错误路径）。
 */

#ifndef NEOJS_RUNTIME_UTF16_H
#define NEOJS_RUNTIME_UTF16_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neojs {

/* UTF-8 串的 UTF-16 code unit 长度（§6.1.4：BMP 内 1 code unit/码点，
 * 增补平面码点拆成代理对 2 code unit）。无效字节按 1 code unit 计（宽容）。 */
std::size_t utf16Length(const std::string &utf8);

/* UTF-16 code unit 索引 -> UTF-8 字节偏移。i 超出长度返回串尾字节偏移。
 * 用于切片起点定位。 */
std::size_t utf16IndexToByte(const std::string &utf8, std::size_t i);

/* UTF-16 code unit 索引处 code unit 数值（charCodeAt）。
 * 代理对的高代理在前（返回高代理值）。越界返回 -1。 */
int32_t utf16CharCodeAt(const std::string &utf8, std::size_t i);

/* 按 UTF-16 code unit 区间 [from, to) 取子串（UTF-8 返回）。
 * from/to 自动夹取到 [0, len]；from >= to 返回空串。 */
std::string utf16Substring(const std::string &utf8, std::size_t from,
                           std::size_t to);

/* 全串 UTF-8 -> UTF-16（code unit 序列）。 */
std::vector<uint16_t> utf8ToUtf16(const std::string &utf8);

/* 全串 UTF-16 -> UTF-8。 */
std::string utf16ToUtf8(const std::vector<uint16_t> &utf16);

/* 全串 UTF-16 -> UTF-8（裸指针版，供 ICU 输出缓冲区使用）。 */
std::string utf16ToUtf8(const uint16_t *utf16, std::size_t len);

/* 按 UTF-16 code unit 搜索（StringIndexOf，§22.1.3.9 引用的抽象运算）：
 * 在 utf8 中从 code unit 索引 from 起查找 search 首次出现的位置，
 * 返回起始 code unit 索引；未找到返回 npos。 */
std::size_t utf16IndexOf(const std::string &utf8, const std::string &search,
                         std::size_t from);

/* 按 UTF-16 code unit 反向搜索（StringLastIndexOf）：从 from（含）起向左
 * 查找 search 最后一次出现的位置（返回起始索引）；未找到返回 npos。 */
std::size_t utf16LastIndexOf(const std::string &utf8, const std::string &search,
                             std::size_t from);

/* UTF-16 code unit 是否为 trim 空白（TrimString，§22.1.3.32：WhiteSpace ∪
 * LineTerminator）。用 ICU u_isUWhiteSpace（LF/CR/2028/2029 均属 White_Space）；
 * 代理 code unit 恒非空白（增补平面无空白）。 */
bool utf16IsWhiteSpace(uint16_t u);

/* Unicode 大小写映射（toUpperCase/toLowerCase，§22.1.3.30/.28）：
 * 按 UTF-16 code point 用 ICU Default Case Conversion（root locale）。
 * 结果可能比源串长（如 ß -> SS）。 */
std::string utf16ToUpper(const std::string &utf8);
std::string utf16ToLower(const std::string &utf8);

}  // namespace neojs

#endif  // NEOJS_RUNTIME_UTF16_H
