<p align="right">
  <strong>简体中文</strong> · <a href="wifi-provisioning.md">English</a>
</p>

# Wi-Fi 配网

本仓库自己实现了一套 SoftAP 配网流程。上游的蓝牙（BLUFI）流程作为历史参考保留在文末。

## 本固件的做法

- **入口**：在状态页长按 UP。状态页本身就写着 `No networks` 与提示行，不需要查文档也能发现。
- **热点**：开放（`WIFI_AUTH_OPEN`），信道 1，单客户端，名字取 SoftAP MAC 的后两字节，
  形如 `Passport-XXXX`。
- **地址**：`192.168.4.1`，直接印在屏幕上。不做弹窗认证，所以手机需要手动打开
  `http://192.168.4.1`。
- **提交**：五个字段以 `application/x-www-form-urlencoded` 提交：SSID、密码、纬度、经度、地点名。
  后三个可选；经纬度必须成对出现，留空表示"这个网络没有地点"，天气页显示 `Location not set`。
  这是终态：离线快照不记录它属于哪个网络，所以没地点的网络绝不会回放别的网络存下的快照。
- **结果**：表单合法即写入 NVS，并把新记录设为 active；设备先回应请求，再重启，由开机遍历
  验证。**没有 APSTA 预验证**，所以密码错误要等重启后的状态页才看得出来，而不是在网页上。
- **退出**：长按 OK，或 5 分钟无提交（`WIFI_PROV_TIMEOUT_MS`）。两条路径都会恢复常驻联网服务。

## 实现地图

| 文件 | 职责 |
| --- | --- |
| `main/wifi_provision_service.c` | AP netif、`esp_http_server`、两个 handler、worker、重启 |
| `main/wifi_provision_flow.c` | 会话状态机；纯逻辑，有主机测试 |
| `main/wifi_provision_form.c` | urlencoded 正文与 `Content-Length` 解析 |
| `main/wifi_provision_html.c` | 静态表单页、动态列表页与结果页 |
| `main/wifi_scan_list.c` | 扫描结果的去重、排序与截断；纯逻辑，有主机测试 |
| `main/wifi_radio_owner.c` | 射频持有者：联网或配网，二选一 |
| `main/page_status.c` | 渲染 `Setup: <ssid>`、`Open <ip>` 与客户端数 |

## 扫描周围网络（起 AP 之前）

表单里的网络名可以点选，不必全靠手打。代价是**必须在起 AP 之前扫**：

- 纯 AP 模式下 `esp_wifi_scan_start()` 不被支持——IDF 文档写明只有 station 或
  station/AP 共存模式支持它。
- APSTA 下扫描会把软热点拽到 STA 正在跳的信道（ESP32-C3 datasheet 原文），已经连上来的
  手机可能掉线，而掉线的正是正在填表的那台。

所以启动序列在 `esp_wifi_init` 之后、`esp_wifi_set_mode(WIFI_MODE_AP)` 之前插了一段
`set_mode(STA) → start → scan → stop`。此刻热点还没起来，手机不可能被踢。

- **非致命**：任何一步失败都只让列表为空，表单退回手打 SSID，配网照常可用。
- 列表上限 16 条；同名 SSID 只留最强的一次广播；按 RSSI 从强到弱排序；隐藏网络
  （广播里没有名字）直接跳过。
- 代价：进配网多约 2 秒（一次全信道扫描）、常驻约 640 B 列表与约 1.6 KB 驱动记录；
  页面缓冲从 1 KB 提到 4 KB，httpd 栈从 5120 提到 6144。
- 页面上**没有"重新扫描"**：AP-only 下没有第二次扫描能力，要刷新只能退出配网再进。

## HTTP 输入边界

- `Content-Length` 必须存在且为纯数字；超过 `WIFI_PROV_BODY_MAX`（1024）时**一个字节正文都
  不读**，直接 413。
- 正文在有界循环里接收，超时最多重试两次；收到的字节数不足声明长度按失败处理，绝不"部分解析"。
- 表单畸形返回错误页，**不写 NVS**。
- profile 库满时保存失败，**不删除任何既有记录**：丢掉一条能用的网络比这次没存上更糟。
- 经纬度按十进制度解析并换算成微度；越界（±90 / ±180）、非数字、或只填一个都返回错误页，不写 NVS。
- 只填地点名而没有坐标同样报错：地点名不能脱离坐标单独存在。

## 清理顺序

先 `httpd_stop()`（它会 join httpd 任务），再注销 Wi-Fi 与 IP 事件处理器，然后 stop 与 deinit
驱动，接着销毁 netif，再关闭 NVS，最后释放射频持有。任何一步反过来都会留下指向已释放内存的
socket 或回调。

## 验收

自动化：`./tools/validate.sh --static` 覆盖状态机、两个解析器、页面生成与服务生命周期，
包括启动那 13 个步骤的逐步骤回滚，以及两个 HTTP handler 的行为。

2026-10-31 上板实测（开发者反馈）：烧录成功；热点出现并能用手机浏览器加入；表单页面
`192.168.4.1` 可访问并提交；设备重启后连上刚提交的网络；长按菜单里的各个测试页正常。

以下项目仍未实测，在交付报告里属于 `Unverified`：

- 另一手机平台上的热点可见性与 `Passport-XXXX` 名称，以及系统"无互联网"提示的文案；
- 配网页新增的经纬度与地点名：手机提交、重启后天气页确实使用该地点；
- `WIFI_EVENT_AP_STACONNECTED` 与 `IP_EVENT_AP_STAIPASSIGNED` 都会触发，且只有后者推进客户端数；
- 启动前、AP 起来后、httpd 起来后、手机连上后四个时间点的 free heap 与最大连续块；
- 5 分钟超时后射频是否真的交还给常驻服务；
- 连续 10 次进出后 socket、任务与堆是否回到基线；
- 预扫是否真的不需要 STA netif（本实现把扫描当作驱动层操作；失败会降级成"没有列表"）；
- 一次扫描是否落在 2 秒量级；
- 手机浏览器对 `<datalist>` 的渲染（iOS Safari 与 Android Chrome 表现不同）。

## 上游蓝牙配网参考（历史）

上游用 BLE + BLUFI 配网，代码在
[`demo/blufi-provisioning` 分支](https://github.com/FoloToy/ai-passport/tree/demo/blufi-provisioning)。
本仓库不用它：它要多背一个 NimBLE 栈，还需要手机 App 或小程序，而三键加小屏已经足够显示
热点名与地址。

<a id="mini-program-name"></a>
## 小程序名称

配套小程序的准确名称为：**蓝牙配网-FoloToy AI PASSPORT**。写说明时请原样保留这个名字，
不要自创译名或搜索词。它是小程序的名字，不是设备的 BLE 广播名；参考固件广播为
`BLUFI_FoloPassport`。

### 代码参考入口

- [`main/demo_blufi.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi.c)
- [`main/demo_blufi_security.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi_security.c)
- [`main/demo_radio.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_radio.c)

`demo_radio.c` 里 NVS、`esp_netif` 与默认事件循环的初始化值得一读，即使做的是 SoftAP 配网
——这几项服务两条路径都要用。
