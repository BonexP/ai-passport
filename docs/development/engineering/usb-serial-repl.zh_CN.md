<p align="right">
  <strong>简体中文</strong> · <a href="usb-serial-repl.md">English</a>
</p>

# USB Serial/JTAG 诊断 REPL

本 fork 可以在现有 USB Serial/JTAG 控制台上构建一个可选的 `esp_console`
REPL。它是开发辅助功能：不必每次改代码、重编译、重烧就能检查运行中的设备。
需求见 [BonexP/ai-passport issue #4](https://github.com/BonexP/ai-passport/issues/4)。

REPL **默认关闭**。普通固件构建的控制台行为、堆占用、任务集合和发布产物保持
不变。

## 启用

使用 ESP-IDF 5.5.3，选择：

```text
Component config -> FoloToy fork options -> Enable USB Serial/JTAG diagnostics REPL
```

对应 Kconfig 符号是 `CONFIG_FOLOTOY_DIAG_REPL`，依赖主控制台为
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`，本板本来就是这样配置的。不要通过
`CONFIG_ESP_CONSOLE_UART*` 启用：UART0 默认 TX 与 GPIO21 的屏幕背光冲突。

做一次性本地构建时，保持受版本控制的默认配置不变，另加临时 defaults：

```bash
printf 'CONFIG_FOLOTOY_DIAG_REPL=y\n' > /tmp/passport-repl.defaults
SDKCONFIG_DEFAULTS="$PWD/sdkconfig.defaults;/tmp/passport-repl.defaults" \
  idf.py -B /tmp/passport-repl-build \
  -D SDKCONFIG=/tmp/passport-repl-build/sdkconfig build
```

`./tools/validate.sh --firmware` 有意只使用受版本控制的默认配置，因此它验证的
是 OFF 基线。需要测试 REPL 时另行构建 ON 镜像。

## 命令

| 命令 | 作用 |
| --- | --- |
| `help` | 列出内置命令和已注册命令。 |
| `status` | 打印当前页、启动状态、Wi-Fi 快照、时间状态、天气快照和用量状态。 |
| `usage` | 打印最近一次 AMD 用量记录、原始点数字段和格式化后的点数行。 |
| `net refresh [weather\|usage\|all]` | 请求刷新一个或两个网络数据源。 |
| `heap` | 打印空闲堆、最低水位、内部堆空闲量和最大内部连续块。 |
| `page <0\|1\|2>` | 切到状态页（`0`）、天气页（`1`）或用量页（`2`）。 |

提示符是 `passport>`。

## 运行边界

- REPL 任务优先级为 2，低于 LVGL 任务（4）、网络 worker（4）和按键任务（5）。
  空闲时阻塞在控制台的 `select()` 路径上，不会忙等轮询。
- `page` 只接受上面三个产品页。当前页是菜单、演示页或配网页时会拒绝，因为离开
  这些页面需要各自的清理与射频/服务交接。请先用正常按键回到产品页。
- 切页通过 `main.c` 现有的 `page_switch()` 路径执行，并持有
  `bsp_lvgl_lock(500)`。命令不会伪造按键事件。
- `net refresh` 不启动网络 worker。worker 通常由进入天气页或用量页启动，且只有
  可见的数据源才会取数；因此状态页执行时命令会报告 worker 已停止，并且不发请求。
  需要立即抓数时先执行 `page 1` 或 `page 2`。
- `status` 和 `usage` 只读现有快照总线。它们不会停 Wi-Fi、停网络 worker、进入
  配网或修改持久化数据。
- `usage` 只报告 key 是否存在，绝不打印 AMD API key、Wi-Fi 密码或 Authorization
  头内容。
- 日志和 REPL 输入输出共用同一条 USB Serial/JTAG 字节流。文本诊断可用，但不是
  二进制传输通道。日志开启时不要发送带帧的截图或其他精确字节负载。

## 验证

先构建默认基线：

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

然后按上面的方式构建 ON 镜像，在明确获得设备烧录授权后烧录并检查：

1. `help` 列出 `status`、`usage`、`net`、`heap`、`page`。
2. 每条命令重复执行多次，设备不得重启、断言或触发任务看门狗。
3. `page 1` / `page 2` 能切屏，UI 继续正常刷新。
4. 进入天气页或用量页后 `net refresh` 能请求刷新；在状态页则明确报告 worker 已停止。
5. 接显示器静置 10 分钟，设备不得重启。
6. 从 `0x0` 烧 merged 镜像只写 bootloader、分区表和应用；保存 Wi-Fi 配置的 NVS
   分区不会被写入，已存数据通常能保留。只有当分区表本身改变或被显式擦除时才会丢。

构建成功不等于真机验证。交付时分别报告 `Build`、`Host tests`、`Device tests`、
`Unverified`。
