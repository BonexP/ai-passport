// main/quote_source.h —— 行情请求组装、标的映射与报文解析（纯逻辑）。
//
// 只认识一种报文形状：腾讯行情 https://qt.gtimg.cn/q=<code>,<code>,...
// 返回 GBK 编码的 `~` 分隔文本，每个标的形如 v_sh000001="1~名称~代码~..."。
//
// 本模块刻意不碰编码转换：名称走 app_config 里的 ASCII label（显示名），
// 接口返回的 GBK 名称只用于定位字段，绝不进显示缓冲。这样 8 MB 固件里
// 不必塞一张 GBK→UTF-8 表，也避开默认字体没有中文字形的问题
// （见 docs/development/engineering/coding-conventions.md 的中文字形检查）。
//
// 字段下标（以 '~' 分割后，0 起）取自 2026-11-17 的实测报文：
//   [1] 名称   [2] 代码   [3] 现价   [4] 昨收
//   [31] 涨跌额 [32] 涨跌幅 [33] 最高  [34] 最低
//   [37] 成交额（A股/ETF 单位为万元，美股为美元；量纲不同，view 层按市场分别标注）
// 伦敦金 hf_XAU 是另一套逗号分隔格式，本模块只解析 qt.gtimg.cn 的 `~` 格式，
// 黄金标的在映射层改用可解析的代码；若接口返回无法识别的格式则判解析失败。
//
// 所有函数不 include ESP-IDF / LVGL，可在主机上用抓到的真实报文测试。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 8 个标的，每个标的的报文约 0.4 KB；URL 与响应都留足余量。
#define QUOTE_URL_MAX       256
#define QUOTE_BODY_MAX      4096
#define QUOTE_SYMBOL_MAX    24   // 单个标的代码，如 "sh600519" / "usNVDA"
#define QUOTE_LABEL_MAX     24   // 显示名（ASCII），如 "NVDA" / "CSI AI"
#define QUOTE_COUNT_MAX     8    // 自选表上限，与 app_config 一致

// 一次取数的结局（放在记录旁边，供 view 层翻译成人话）。
typedef enum {
    QUOTE_STATUS_NONE = 0,   // 本次开机还没有请求过
    QUOTE_STATUS_OK,         // 最近一次成功（至少一个标的解析出来）
    QUOTE_STATUS_NO_NET,     // 没联网 / 没校时，未发请求
    QUOTE_STATUS_UNAVAILABLE,// 5xx、超时、TLS、传输层失败
    QUOTE_STATUS_TRUNCATED,  // 报文超过缓冲，不显示半截数字
    QUOTE_STATUS_BAD_BODY,   // 200 但无法解析的报文
} quote_status_t;

// 一个标的的一次观测。价格用整数分/厘表示，避免浮点。
// price_1e4 是当前价 ×10000 的四舍五入整数；change_1e4 / pct_1e4 同尺度
// （pct_1e4 是百分比 ×10000，如 -4.12% → -41200）。
typedef struct {
    bool     valid;             // 该标的这一轮是否成功解析
    char     label[QUOTE_LABEL_MAX];
    char     code[QUOTE_SYMBOL_MAX]; // 接口代码（如 "usNVDA"），供 view 判断成交额单位
    int64_t  price_1e4;
    int64_t  change_1e4;
    int64_t  pct_1e4;
    // 成交额原文（接口给的字符串，如 "55046378" 或 "52678055451"），view 层按
    // 市场单位解释：A 股/ETF 是万元，美股是美元。保留原串避免解析器做浮点。
    char     amount[32];
} quote_item_t;

// 一次成功的行情观测（最多 QUOTE_COUNT_MAX 个标的）。
typedef struct {
    bool          has_data;
    uint8_t       count;
    quote_item_t  items[QUOTE_COUNT_MAX];
} quote_state_t;

// 把 app_config 的 symbol 映射成接口代码。输入形如 "930713"/"NVDA"/"XAU"，
// 输出形如 "sh930713"? —— 注意：默认表里的中证 AI 指数 930713 与现货黄金 XAU
// 在腾讯接口没有可用的 `~` 格式代码，映射层对它们返回 false，调用方跳过该标的。
// 返回 true 表示写入 out（已 NUL 结尾）；cap 不足返回 false。
bool quote_symbol_to_code(const char *symbol, char *out, size_t cap);

// 组装请求 URL。codes 为已映射好的接口代码数组（不含逗号），count 为个数。
// 至少一个代码时才成功；cap 不足返回 false 并把 buf 置空串。
bool quote_url_build(char **codes, size_t count, char *buf, size_t cap);

// 解析 qt.gtimg.cn 的响应体。成功时 out->has_data = true，count 为成功解析的
// 标的数（0..QUOTE_COUNT_MAX）。codes/labels 与报文顺序一一对应，用于填 label。
// 失败条件：json == NULL / out == NULL / labels 或 codes 为 NULL 且 count>0。
// 单个标的解析失败只置其 valid=false，不影响其余标的——接口偶尔会对某个代码
// 返回空值，不能因为一个坏标的毁掉整屏行情。
bool quote_parse(const char *body, char **codes, char **labels, size_t count,
                 quote_state_t *out);

// 价格（1e4 整数）→ "3581.7300" / "156.1400"。缓冲不足返回 false 并置空串。
bool quote_format_price(int64_t price_1e4, char *buf, size_t cap);

// 涨跌幅（百分比 ×10000）→ "+1.09%" / "-4.12%" / "0.00%"。缓冲不足返回 false。
bool quote_format_pct(int64_t pct_1e4, char *buf, size_t cap);

// 成交额原串 → 带单位的紧凑文案。A 股/ETF（代码以 sh/sz 开头）单位为万元，
// 美股（us 开头）单位为美元。整数运算缩写，不输出浮点。
// 例如万元串 "55046378" → "5504.6亿"（内部先 ×1e4 元再缩写），
// 美元串 "52678055451" → "$52.7B"。buf 不足返回 false。
bool quote_format_amount(const char *code, const char *amount_raw, char *buf, size_t cap);
