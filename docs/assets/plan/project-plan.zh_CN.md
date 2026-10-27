<p align="right">
  <strong>简体中文</strong> · <a href="project-plan.md">English</a>
</p>

# 项目规划：FoloToy AI Passport 集大成固件

范围：基于 `FoloToy/ai-passport` 基线（ESP-IDF 5.5.3）的自定义固件。
状态：规划中，尚无应用代码。
调研日期：2026-10-27，对应上游 `main` 提交 `006c227`。

结论先行：功能集在不依赖任何外部辅助服务的前提下可行。唯一的硬约束是内存——本板
无 PSRAM，所有功能争抢约 320 KB 内部 SRAM。这个约束决定的是实施顺序和并发方式，
不是能不能做。

## 1. 目标

一个固件，覆盖四件事：

1. 多地点 Wi-Fi profile 与自动漫游（家里、公司、其他）。
2. 固定自选表的股票行情。
3. 当前地点的天气。
4. 局域网已知端点健康检查与 mDNS 服务发现。

设备是独立工作的。没有局域网 hub，没有配套服务器，不依赖另一台机器开机。每个功能
要么设备自己完成，要么退化成明确显示的离线状态。

## 2. 硬件边界

来源：`components/bsp/include/bsp_pins.h`、`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`、
`README.md` 能力契约表。

| 项目 | 事实 | 后果 |
| --- | --- | --- |
| 主控 | ESP32-C3，8 MB Flash，无 PSRAM | 全部挤在内部 SRAM；Wi-Fi、TLS、LVGL、音频互相争抢 |
| 屏幕 | ST7789 240×320 竖屏 RGB565，LVGL 9，24 KB 池 + 单个 9.6 KB DMA 缓冲 | 一屏行情放得下；不能双缓冲，不能大图 |
| 按键 | UP / DOWN / OK 共享 GPIO0 ADC 电阻梯 | 回调不得阻塞；网络工作放 worker 任务 |
| 音频 | ES8311 I2S 全双工，demo 录音缓冲约 96 KB | 音频与全量联网不能同时常驻 |
| 无线 | Wi-Fi 2.4G STA 与 NimBLE 同芯片，但 RAM 预算不允许同开 | 配网与联网是两个模式 |
| 电池 | CW2017 电量计 | Wi-Fi 持续轮询需要睡眠策略 |

上游自己的规则（`AI_HARDWARE_DEVELOPMENT_GUIDE.md`）要求：添加网络、TLS、音频或素材时，
必须复查 24 KB LVGL 池、9.6 KB LCD DMA、I2S DMA、96 KB 录音缓冲、radio 栈、任务栈、
总 free heap 与最大连续块。基线明确预期衍生应用会加网络，并提供了核算清单。

## 3. 基线行为

`main/demo_wifi.c` 只扫描 AP，不关联、不保存凭证、不发任何 HTTP 请求。在 `main/` 与
`components/` 中检索不到 `esp_http_client`、`esp_tls` 或 JSON 解析器的使用。

联网、凭证存储、HTTP、TLS、JSON 解析全部由我们实现。好消息是 `esp_http_client`、
`esp-tls`（mbedTLS）、`nvs_flash`、`esp_ping`、`mdns`、`json` 都是 ESP-IDF 5.5.3 内置组件，
不需要新增受管依赖。

## 4. 功能评估

### 4.1 多地点 Wi-Fi profile：可行，风险最低

机制，只用内置 API：

1. NVS 里存 N 个 profile：`{name, ssid, password, last_ok_time}`，外加 `active_index`。
2. 开机先试 `active_index`；失败则按列表遍历，每个 profile 约 10 秒超时；全部失败进入配网状态。
3. 断线时重跑整个遍历，而不是只重试上一个 SSID，这样人从家到公司，设备跟着换网。
4. 配网走 BLE（BLUFI 模式）写入 NVS，重启进入纯 Wi-Fi 路径。

状态机是纯逻辑，按上游规则必须抽出来做主机测试。这与参考应用 `xiaonuo-jianlu` 的做法一致。

### 4.2 天气：可行，最简单的联网功能

一次 HTTPS GET，返回很小的 JSON（远小于 10 KB），用内置 JSON 组件解析进有界静态缓冲。
轮询间隔 5 分钟以上。

待定：用哪家。带 key 的免费额度（如和风天气）可用，但 key 会落在设备 NVS 里；
无 key 的公开接口可以免掉这个负担，但稳定性保证较弱。实施前决定。

### 4.3 股票行情：可行，数据量是约束重点

自选表：中证 AI 指数、AI ETF、纳指 100、标普 500、英伟达、特斯拉、现货黄金、镁光。

8 个标的走一到两个请求就是几 KB JSON，静态流式缓冲装得下。界面：240×320 竖屏一屏
放 8 行「名称 / 价格 / 涨跌幅 / 涨跌额 / 成交额」。

数据源选项，按优先级：

- **无 key 公开行情接口。** 常见的中国市场数据源会返回紧凑 JSON 或分隔文本，HTTPS 无鉴权。
  需要先验证响应大小、编码和限频，再定案。
- **带 key 的 API（iwencai 等）。** 数据更全，但 key 必须配进 NVS 且绝不入库，
  等于要为「一个密钥」多做一条配网路径。

这个决定只影响客户端层，可以推迟到显示和轮询调度验证通过之后再定。

轮询调度是纯逻辑（A 股与美股的交易时段、非交易时段退避），必须主机测试。

### 4.4 局域网探针：收窄定义后可行

- 用 `esp_ping` 对一小份已知端点做 ICMP 可达性检查，成本很低。
- 用内置 `mdns` 组件浏览广播的服务。
- 拒绝全 /24 网段 TCP 端口扫描：2.4 GHz 上单次 TCP 探测约 100~300 ms，扫一轮要几分钟，
  屏幕也无法有效呈现结果，还会抢占音频和 TLS 的内存预算，在本产品里没有价值。

明确范围：「我关心的端点在不在，这个网络广播了什么服务」。端点列表由用户在 NVS 里编辑。

## 5. 架构：纯设备侧

```
[AI Passport]
  Wi-Fi profile 管理器（NVS + 状态机）
        |
  worker 任务：HTTP/TLS 客户端，有界静态缓冲，JSON 解析
        |
  数据模型（行情/天气/端点状态）驻 RAM，最近一次快照进 NVS
        |
  LVGL UI 任务（每次访问都持有 bsp_lvgl_lock）
```

没有 hub 之后衍生出的设计规则：

- 凭证（如果有）只存 NVS，绝不入库，也不进提交的头文件。
- 每个网络功能持有自己的小缓冲，在开机 heap 健康时分配。
- 任何功能都不得阻塞 LVGL 任务或按键回调。
- 离线行为必须显式：显示 NVS 里的快照，并带上明显的过期标记与快照时间。
- 路由器不转发多播时，mDNS 发现必须退化为缓存的直连地址（RFC 6762 第 8.1 节）。
  这是参考应用里记录过的已知故障模式。

## 6. 中文显示

默认 Montserrat 没有中文字形；UTF-8 编译通过不代表能显示。两条路线：

- **A（推荐）：** 用 `lv_font_conv` 从 Noto Sans SC 生成覆盖 GB2312 的子集（16 px 约 7000 字形），
  生成的 C 文件入库。8 MB Flash 放得下。
- **B：** 只收应用里出现过的字。体积更小，但每加一句文案都要重新生成。

选 A：行情名称和天气词都是中文，每改一个字就重生成字体是长期成本。

## 7. 内存纪律

1. 开机预分配 LVGL 池、音频 DMA（若启用）、HTTP 缓冲。
2. 网络、存储、音频全部在 worker 任务里。
3. BLE 配网与 Wi-Fi 联网是两个启动模式，两栈永不同时运行。
4. 每完成一个功能，在交付报告里记录 free heap 与最大连续块。

参考应用 `xiaonuo-jianlu` 在相同硬件与 IDF 版本上的实测：Wi-Fi 驱动常驻后剩约 42 KB heap，
NimBLE 栈需约 58 KB，音频 + Wi-Fi + HTTP + UI 组合稳态约 33 KB，音频 DMA 在开机时预分配
（后期分配曾导致冻结）。

## 8. 分期计划

| 期 | 交付物 | 验收 |
| --- | --- | --- |
| P0 | Linux 主机装好 ESP-IDF 5.5.3；未改动的基线上 `./tools/validate.sh` 全绿 | Build 与主机测试通过，产出合并镜像 |
| P1 | Wi-Fi profile 管理器：NVS 存储、开机遍历、带主机测试的状态机、状态页 | 自动重连并切换 profile；主机测试通过 |
| P2 | HTTP/TLS 客户端与有界缓冲、最小 JSON、天气页、端点健康页 | 离线时显示过期快照 |
| P3 | 股票页：8 行行情 + 有主机测试的轮询调度 | 交易时段数据正确 |
| P4 | 打磨：中文字体、睡眠策略、电量读数、离线队列 | 过夜续航实测 |

每期在独立主题分支开发，合并进长期集成分支。交付报告遵循上游四段格式：
Build、Host tests、Device tests、Unverified。

## 9. 待决问题

1. 天气数据源：带 key 的免费额度（存 NVS），还是无 key 公开接口？
2. 股票数据源：无 key 接口够用，还是带 key API 值得额外的配网成本？
3. 音频：本期做，还是等网络栈稳定后另开一期？
4. 探针清单：除了 mDNS 发现，是否需要显式的 ICMP 目标列表？

## 10. 证据索引

- 基线能力契约与硬件事实：`README.md`、`docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`
- 开发规则：`AGENTS.md`、`docs/development/ai-guide.md`
- 环境与构建：`docs/development/engineering/environment-setup.md`、`build-and-test.md`
- 基线 Wi-Fi 行为：`main/demo_wifi.c`（仅扫描）
- 联网应用实测：`docs/reference/noah-1106/xiaonuo-jianlu/README.md`
- SoftAP 资源预算：`docs/reference/phoenixzhc/softap-provisioning-and-resource-budget.md`
- 依赖清单：`components/bsp/idf_component.yml`
- Fork 工作流：`docs/fork-guide.md`
