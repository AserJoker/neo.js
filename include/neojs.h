// neojs.h - neo.js 唯一对外 C API（M0 占位）
//
// 本头文件是 neo.js 嵌入库对外的唯一入口。M0 阶段仅提供占位声明，
// M1 起充实 Value/Context/Engine 等 API（详见 ADR-009）。
//
// 命名约定（ADR-009）：
//   - C API 类型/函数以 NJS_ 前缀
//   - 不透明指针（opaque pointer）封装内部对象
//
// 关联 ADR: 009（源码结构）、005（native 等价）、006（异常即值）

#ifndef NEOJS_H
#define NEOJS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 不透明类型（M1 起充实）
typedef struct NJS_Context NJS_Context;
typedef struct NJS_Value   NJS_Value;
typedef struct NJS_Engine  NJS_Engine;

// 错误码（ADR-006：异常即值；C API 边界用错误码）
typedef enum {
    NJS_OK = 0,
    NJS_ERROR = 1,
    NJS_PANIC = 2
} NJS_Result;

// M0 占位：返回版本字符串
const char* NJS_Version(void);

// ---------------------------------------------------------------------------
// Intl 后端（宿主注入）
//
// core 不链接 ICU，也不内置 locale 数据。宿主通过 NJS_IntlBackend 提供
// locale 数据与格式化原语；不提供时 core 使用内置的最小实现（en/UTC/latn）。
// 后端只负责“数据 + 格式化”，Intl 对象模型、选项校验、locale 协商、
// formatToParts 的 JS 对象组装全部在 core 内完成。
// ---------------------------------------------------------------------------

#define NJS_INTL_ABI_VERSION 9u

typedef enum {
    NJS_INTL_OK = 0,
    NJS_INTL_UNSUPPORTED = 1,
    NJS_INTL_INVALID_OPTION = 2,
    NJS_INTL_INTERNAL = 3,
    // 值本身无效（如 DateTimeFormat 的 NaN 时间值），core 映射为 RangeError。
    NJS_INTL_INVALID_VALUE = 4
} NJS_IntlStatus;

// 后端能力位图。未置位的能力由 core 按规范抛错或退化。
typedef enum {
    NJS_INTL_CAP_NUMBER = 1u << 0,
    NJS_INTL_CAP_DATETIME = 1u << 1,
    NJS_INTL_CAP_COLLATOR = 1u << 2,
    NJS_INTL_CAP_PLURAL = 1u << 3,
    NJS_INTL_CAP_SEGMENTER = 1u << 4,
    NJS_INTL_CAP_LIST = 1u << 5,
    NJS_INTL_CAP_RELATIVE = 1u << 6,
    NJS_INTL_CAP_DISPLAY_NAMES = 1u << 7,
    NJS_INTL_CAP_FORMAT_TO_PARTS = 1u << 8,
    NJS_INTL_CAP_COMPACT = 1u << 9,
    NJS_INTL_CAP_UNIT = 1u << 10
} NJS_IntlCapability;

// formatToParts 的字段类型（ECMA-402 PartitionNumberPattern 等）。
typedef enum {
    NJS_INTL_PART_LITERAL = 0,
    NJS_INTL_PART_INTEGER = 1,
    NJS_INTL_PART_GROUP = 2,
    NJS_INTL_PART_DECIMAL = 3,
    NJS_INTL_PART_FRACTION = 4,
    NJS_INTL_PART_CURRENCY = 5,
    NJS_INTL_PART_UNIT = 6,
    NJS_INTL_PART_PERCENT = 7,
    NJS_INTL_PART_MINUS_SIGN = 8,
    NJS_INTL_PART_PLUS_SIGN = 9,
    NJS_INTL_PART_YEAR = 10,
    NJS_INTL_PART_MONTH = 11,
    NJS_INTL_PART_DAY = 12,
    NJS_INTL_PART_HOUR = 13,
    NJS_INTL_PART_MINUTE = 14,
    NJS_INTL_PART_SECOND = 15,
    NJS_INTL_PART_DAY_PERIOD = 16,
    NJS_INTL_PART_TIME_ZONE_NAME = 17,
    NJS_INTL_PART_ELEMENT = 18
} NJS_IntlPartType;

typedef struct {
    NJS_IntlPartType type;
    const char* text;   // UTF-8，formatter 持有，下次 format/destroy 前有效
    size_t length;
} NJS_IntlPart;

typedef struct {
    const char* key;    // UTF-8
    const char* value;  // UTF-8
} NJS_IntlOption;

// 后端归一化后的 resolvedOptions 字段（NUL 结尾）。
typedef struct {
    char locale[128];
    char numbering_system[32];
    char calendar[32];
    char time_zone[64];
} NJS_IntlResolved;

typedef struct NJS_IntlFormatter NJS_IntlFormatter;
typedef struct NJS_IntlCollator NJS_IntlCollator;
typedef struct NJS_IntlPluralRules NJS_IntlPluralRules;
typedef struct NJS_IntlRelativeTimeFormat NJS_IntlRelativeTimeFormat;
typedef struct NJS_IntlListFormat NJS_IntlListFormat;
typedef struct NJS_IntlDisplayNames NJS_IntlDisplayNames;
typedef struct NJS_IntlSegmenter NJS_IntlSegmenter;
typedef struct NJS_IntlBackend NJS_IntlBackend;

// 一个分段结果。index/length 以 UTF-16 码元为单位。
typedef struct {
    size_t index;
    size_t length;
    int is_word_like;
} NJS_IntlSegment;

struct NJS_IntlBackend {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t capabilities;
    void* user_data;

    // 两段式字符串输出：out 为 NULL 或 *out_len 不足时，写入所需长度（含 NUL）
    // 并返回 NJS_INTL_OK；否则写入 NUL 结尾字符串并更新 *out_len。
    NJS_IntlStatus (*canonicalize_locale)(void* user_data, const char* tag,
                                          size_t tag_len, char* out,
                                          size_t* out_len);
    // 返回以 NULL 结尾的 locale 字符串数组；数组与字符串由后端持有。
    const char* const* (*available_locales)(void* user_data, size_t* count);
    NJS_IntlStatus (*default_locale)(void* user_data, char* out, size_t* out_len);
    NJS_IntlStatus (*default_time_zone)(void* user_data, char* out, size_t* out_len);

    // 创建数字格式化器。opts 为 UTF-8 key/value 列表；resolved 可为 NULL。
    // 失败返回 NULL，并通过 status 报告原因。
    NJS_IntlFormatter* (*number_format_create)(void* user_data, const char* locale,
                                               const NJS_IntlOption* opts,
                                               size_t nopts,
                                               NJS_IntlResolved* resolved,
                                               NJS_IntlStatus* status);
    // 格式化 value。parts 为 NULL 或 *count 不足时，写入所需数量并返回 OK；
    // 否则填充 parts 并更新 *count。
    NJS_IntlStatus (*number_format_format)(NJS_IntlFormatter* formatter, double value,
                                           NJS_IntlPart* parts, size_t* count);
    void (*formatter_destroy)(NJS_IntlFormatter* formatter);

    // 可选：返回最近一次错误的静态字符串（UTF-8），无错误返回 NULL。
    const char* (*last_error)(void* user_data);

    // —— ABI v2 追加 ——
    // 创建日期时间格式化器。time 为毫秒时间值（NaN 表示 Invalid Date）。
    NJS_IntlFormatter* (*date_time_format_create)(void* user_data,
                                                  const char* locale,
                                                  const NJS_IntlOption* opts,
                                                  size_t nopts,
                                                  NJS_IntlResolved* resolved,
                                                  NJS_IntlStatus* status);
    NJS_IntlStatus (*date_time_format_format)(NJS_IntlFormatter* formatter,
                                              double time, NJS_IntlPart* parts,
                                              size_t* count);

    // —— ABI v3 追加 ——
    // 创建/销毁排序器；compare 返回 -1/0/1。
    NJS_IntlCollator* (*collator_create)(void* user_data, const char* locale,
                                         const NJS_IntlOption* opts, size_t nopts,
                                         NJS_IntlResolved* resolved,
                                         NJS_IntlStatus* status);
    NJS_IntlStatus (*collator_compare)(NJS_IntlCollator* collator, const char* a,
                                       size_t a_len, const char* b, size_t b_len,
                                       int* result);
    void (*collator_destroy)(NJS_IntlCollator* collator);

    // —— ABI v4 追加 ——
    // 创建/销毁复数规则；select 返回静态字符串（zero/one/two/few/many/other）。
    NJS_IntlPluralRules* (*plural_rules_create)(void* user_data,
                                                const char* locale,
                                                const NJS_IntlOption* opts,
                                                size_t nopts,
                                                NJS_IntlResolved* resolved,
                                                NJS_IntlStatus* status);
    const char* (*plural_rules_select)(NJS_IntlPluralRules* rules, double value);
    void (*plural_rules_destroy)(NJS_IntlPluralRules* rules);

    // —— ABI v5 追加 ——
    // 创建/销毁相对时间格式化器。unit 为单数形式（year/quarter/month/week/
    // day/hour/minute/second）；value 为带符号数值。
    NJS_IntlRelativeTimeFormat* (*relative_time_format_create)(
        void* user_data, const char* locale, const NJS_IntlOption* opts,
        size_t nopts, NJS_IntlResolved* resolved, NJS_IntlStatus* status);
    NJS_IntlStatus (*relative_time_format_format)(
        NJS_IntlRelativeTimeFormat* formatter, double value, const char* unit,
        NJS_IntlPart* parts, size_t* count);
    void (*relative_time_format_destroy)(NJS_IntlRelativeTimeFormat* formatter);

    // —— ABI v6 追加 ——
    // 创建/销毁列表格式化器。items 为 UTF-8 字符串数组，输出 parts 的
    // element 类型承载各元素文本。
    NJS_IntlListFormat* (*list_format_create)(void* user_data, const char* locale,
                                              const NJS_IntlOption* opts,
                                              size_t nopts,
                                              NJS_IntlResolved* resolved,
                                              NJS_IntlStatus* status);
    NJS_IntlStatus (*list_format_format)(NJS_IntlListFormat* formatter,
                                         const char* const* items, size_t count,
                                         NJS_IntlPart* parts, size_t* out_count);
    void (*list_format_destroy)(NJS_IntlListFormat* formatter);

    // —— ABI v7 追加 ——
    // 创建/销毁显示名解析器。of 为两段式字符串输出；无显示名且 fallback
    // 为 none 时写入 *out_len = 0 并返回 OK。
    NJS_IntlDisplayNames* (*display_names_create)(void* user_data,
                                                  const char* locale,
                                                  const NJS_IntlOption* opts,
                                                  size_t nopts,
                                                  NJS_IntlResolved* resolved,
                                                  NJS_IntlStatus* status);
    NJS_IntlStatus (*display_names_of)(NJS_IntlDisplayNames* names,
                                       const char* code, char* out,
                                       size_t* out_len);
    void (*display_names_destroy)(NJS_IntlDisplayNames* names);

    // —— ABI v8 追加 ——
    // 返回某个 key 的受支持值（静态数组，后端持有）。key 非法时返回
    // NJS_INTL_INVALID_OPTION。
    NJS_IntlStatus (*supported_values)(void* user_data, const char* key,
                                       const char* const** values, size_t* count);

    // —— ABI v9 追加 ——
    // 创建/销毁分段器；segment 两段式输出分段结果。
    NJS_IntlSegmenter* (*segmenter_create)(void* user_data, const char* locale,
                                           const NJS_IntlOption* opts,
                                           size_t nopts,
                                           NJS_IntlResolved* resolved,
                                           NJS_IntlStatus* status);
    NJS_IntlStatus (*segmenter_segment)(NJS_IntlSegmenter* segmenter,
                                        const char* text, size_t text_len,
                                        NJS_IntlSegment* segments,
                                        size_t* count);
    void (*segmenter_destroy)(NJS_IntlSegmenter* segmenter);
};

// core 内置的最小后端（en / latn / gregory / UTC）。始终可用。
const NJS_IntlBackend* NJS_MinimalIntlBackend(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // NEOJS_H
