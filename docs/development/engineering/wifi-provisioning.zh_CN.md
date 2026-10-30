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
- **提交**：SSID 与密码以 `application/x-www-form-urlencoded` 提交。
- **结果**：表单合法即写入 NVS，并把新记录设为 active；设备先回应请求，再重启，由开机遍历
  验证。**没有 APSTA 预验证**，所以密码错误要等重启后的状态页才看得出来，而不是在网页上。
- **退出**：长按 OK，或 5 分钟无提交（`WIFI_PROV_TIMEOUT_MS`）。两条路径都会恢复常驻联网服务。

## 实现地图

| 文件 | 职责 |
| --- | --- |
| `main/wifi_provision_service.c` | AP netif、`esp_http_server`、两个 handler、worker、重启 |
| `main/wifi_provision_flow.c` | 会话状态机；纯逻辑，有主机测试 |
| `main/wifi_provision_form.c` | urlencoded 正文与 `Content-Length` 解析 |
| `main/wifi_provision_html.c` | 静态表单页与结果页 |
| `main/wifi_radio_owner.c` | 射频持有者：联网或配网，二选一 |
| `main/page_status.c` | 渲染 `Setup: <ssid>`、`Open <ip>` 与客户端数 |

## HTTP 输入边界

- `Content-Length` 必须存在且为纯数字；超过 `WIFI_PROV_BODY_MAX`（1024）时**一个字节正文都
  不读**，直接 413。
- 正文在有界循环里接收，超时最多重试两次；收到的字节数不足声明长度按失败处理，绝不"部分解析"。
- 表单畸形返回错误页，**不写 NVS**。
- profile 库满时保存失败，**不删除任何既有记录**：丢掉一条能用的网络比这次没存上更糟。

## 清理顺序

先 `httpd_stop()`（它会 join httpd 任务），再注销 Wi-Fi 与 IP 事件处理器，然后 stop 与 deinit
驱动，接着销毁 netif，再关闭 NVS，最后释放射频持有。任何一步反过来都会留下指向已释放内存的
socket 或回调。

## 验收

自动化：`./tools/validate.sh --static` 覆盖状态机、两个解析器、页面生成与服务生命周期，
包括启动那 13 个步骤的逐步骤回滚，以及两个 HTTP handler 的行为。

以下项目**均未上板实测**，在交付报告里都属于 `Unverified`：

- 热点在 Android 与 iOS 上的可见性，以及 `Passport-XXXX` 这个名字；
- 手动打开 `192.168.4.1`（含系统"无互联网"提示）；
- `WIFI_EVENT_AP_STACONNECTED` 与 `IP_EVENT_AP_STAIPASSIGNED` 两条事件是否都出现；
- 启动前、AP 起来后、httpd 起来后、手机连上后四个时间点的 free heap 与最大连续块；
- 重启后能否连上刚提交的网络；
- 5 分钟超时后射频是否真的交还给常驻服务；
- 连续 10 次进出后 socket、任务与堆是否回到基线。

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
