// main/usage_view.h —— 用量页该显示什么（纯逻辑）。
//
// 与 weather_view 同一套理由：页面只负责把这里算出来的文字贴到 label 上。
// 但这里比天气页多一层麻烦——"卡片为什么是空的"本身就是这个页面的主要用途之一，
// 所以状态机不是可有可无的糖，它决定了用户看到的是"key 被吊销"还是"服务挂了"。
//
// 三条不对称的规则，都写进了测试：
//   1. 失败不清空已取到的数字。key 被拒的那一天，昨天剩下的点数仍然有意义；
//      只有"还没有数据"时才用状态文案占位。
//   2. 没有 key 不是错误，是构建形态。CI 构建永远走这一支，它不该显示得像个故障。
//   3. 额度未知（报文没给 daily_cost_limit_usd）不等于额度为 0。后者会画出一条
//      100% 用满的红条，前者只能显示"已用若干"。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usage_source.h"

typedef enum {
    USAGE_VIEW_DATA = 0,    // 有数据：点数、明细、今日计数都显示
    USAGE_VIEW_NO_KEY,      // 编译期没配 key
    USAGE_VIEW_KEY_REJECTED,
    USAGE_VIEW_RATE_LIMITED,
    USAGE_VIEW_UNAVAILABLE,
    USAGE_VIEW_WAIT_TIME,   // 有 key，但还没校时：TLS 校验不了证书，不开 socket
    USAGE_VIEW_UPDATING,    // 首次请求还没回来
    USAGE_VIEW_RETRYING,    // 等得太久仍无数据
    USAGE_VIEW_BAD_BODY,    // 报文解析失败 / 被截断
} usage_view_state_t;

// 决定显示哪一种状态。
//
// 参数都是页面能廉价拿到的事实，判断顺序即优先级：
//   有数据 → 永远是 DATA（失败只改状态行，见文件头第 1 条规则）。
//   没有数据时：没有 key → 服务端的明确结论（401/429/坏报文）→ 没联网 →
//   没校时 → 服务不可用 → 更新中/重试中。
// 即：本机构建形态最确定，"对方明确说了不行"次之，最后才是对远端状态的猜测。
//
//   has_key       编译期是否配了 key（app_config_amd_key() != NULL，且非空串）
//   connected     Wi-Fi 是否处于已连接
//   time_valid    SNTP 是否已同步（TLS 校验证书有效期需要它）
//   has_data      总线上是否已有过一次成功的观测
//   status        最近一次请求的结局
//   waited_long   已等待超过阈值仍无数据
usage_view_state_t usage_view_state(bool has_key, bool connected, bool time_valid,
                                    bool has_data, usage_status_t status, bool waited_long);

// 主卡在没有数据时显示的一句话；有数据时返回 NULL。
const char *usage_view_message(usage_view_state_t state);

// 标题行下方那一行：有数据时说明最近一次请求的结局（含"用的是什么时间的数据"），
// 没有数据时就是状态文案。返回 false 表示这一行不显示。
//
// stale 参数目前恒由调用方传 false：用量不做持久化（见 project-plan.md §9 第 13 条），
// 所以不存在"从 NVS 读回来的旧副本"。保留这个参数是为了让本函数继续与
// weather_view_updated 同构——将来若真要持久化，调用侧不用改签名。
// age_ms < 0 表示"不知道多久前"，此时只报前缀，不编一个时间。
bool usage_view_status_line(usage_view_state_t state, usage_status_t status, bool stale,
                            int64_t age_ms, int32_t fetched_epoch, char *buf, size_t cap);

// 主卡：剩余点数。没有数据或没有额度时返回 false（页面用大字状态文案占位）。
// 额度未知时仍然显示剩余，因为 remaining 是接口独立给出的字段。
bool usage_view_remaining(usage_view_state_t state, const usage_state_t *usage, char *buf,
                          size_t cap);

// 明细行："1.5488 / 2.0000 used" 或额度未知时的 "0.4512 used"。
// 没有数据时返回 false。
bool usage_view_used_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                          size_t cap);

// 额度用掉的比例，用于 2 px 进度条。额度未知、额度为 0 或没有数据时返回 -1
// （页面据此隐藏进度条）——"未知"与"用满了"必须区分开。
// 返回值夹在 0..1000（千分比），超过 1000 表示超额使用。
int usage_view_used_permille(usage_view_state_t state, const usage_state_t *usage);

// 重置时刻行："Reset 00:00"。daily_reset_at 缺失或没有数据时返回 false。
bool usage_view_reset_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                           size_t cap);

// 今日计数行："Today 42 req  180.6k tok  3 err"。没有数据时返回 false。
bool usage_view_today_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                           size_t cap);

// 30 天花费行。cost 用 usage_format_points() 的万分位表示，"30d 12.5000 pts"。
// 没有数据时返回 false。
bool usage_view_30d_line(usage_view_state_t state, const usage_state_t *usage, char *buf,
                         size_t cap);
