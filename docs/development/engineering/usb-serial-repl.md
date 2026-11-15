<p align="right">
  <a href="usb-serial-repl.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# USB Serial/JTAG diagnostics REPL

This fork can build an optional `esp_console` REPL on the existing USB
Serial/JTAG console. It is a development aid for inspecting the running device
without rebuilding and reflashing for every check. The feature was requested in
[BonexP/ai-passport issue #4](https://github.com/BonexP/ai-passport/issues/4).

The REPL is **off by default**. A normal firmware build keeps the previous
console behavior, heap use, task set, and release artifact shape.

## Enable

Use ESP-IDF 5.5.3 and select:

```text
Component config -> FoloToy fork options -> Enable USB Serial/JTAG diagnostics REPL
```

The Kconfig symbol is `CONFIG_FOLOTOY_DIAG_REPL`. It depends on the primary
console being `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`, which this board already
uses. Do not enable it through `CONFIG_ESP_CONSOLE_UART*`: UART0's default TX
pin conflicts with the display backlight on GPIO21.

For a one-off local build, keep the tracked defaults unchanged and add a
temporary defaults file:

```bash
printf 'CONFIG_FOLOTOY_DIAG_REPL=y\n' > /tmp/passport-repl.defaults
SDKCONFIG_DEFAULTS="$PWD/sdkconfig.defaults;/tmp/passport-repl.defaults" \
  idf.py -B /tmp/passport-repl-build \
  -D SDKCONFIG=/tmp/passport-repl-build/sdkconfig build
```

`./tools/validate.sh --firmware` intentionally builds the tracked default
configuration, so it validates the OFF baseline. Build the ON image separately
when you need to test the REPL.

## Commands

| Command | Purpose |
| --- | --- |
| `help` | List the built-in and registered commands. |
| `status` | Print the current page, boot state, Wi-Fi snapshot, time state, weather snapshot, and usage status. |
| `usage` | Print the last AMD usage record, raw point fields, and formatted point lines. |
| `net refresh [weather\|usage\|all]` | Request a refresh for one or both network sources. |
| `heap` | Print free heap, minimum free heap, internal free heap, and the largest internal block. |
| `page <0\|1\|2>` | Switch to Status (`0`), Weather (`1`), or Usage (`2`). |

The prompt is `passport>`.

## Runtime boundaries

- The REPL task uses priority 2, below the LVGL task (4), the network worker (4),
  and the input task (5). It blocks on the console's `select()` path when idle;
  it does not poll in a tight loop.
- `page` only accepts the three product pages listed above. It refuses while the
  current page is Menu, Demo, or Provisioning, because leaving those pages needs
  their own teardown and radio/service handoff. Return to a product page through
  the normal controls first.
- A page switch is performed through `main.c`'s existing `page_switch()` path
  while holding `bsp_lvgl_lock(500)`. The command never injects synthetic button
  events.
- `net refresh` does not start the network worker. The worker is normally
  started by entering Weather or Usage, and only visible sources fetch data, so
  the command reports that it is stopped and sends nothing on the Status page.
  Run `page 1` or `page 2` first when an immediate request is needed.
- `status` and `usage` read the existing snapshot buses. They do not stop Wi-Fi,
  stop the network worker, enter provisioning, or change persistent data.
- The `usage` command reports key presence only. It never prints the AMD API key,
  Wi-Fi password, or Authorization header value.
- Logs and REPL input/output share one USB Serial/JTAG byte stream. Text
  diagnostics work, but this is not a binary transport. Do not send a framed
  screenshot or other exact-byte payload while logging is active.

## Verification

Build the default baseline:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

Then build the ON image as shown above, flash it with explicit device approval,
and check:

1. `help` lists `status`, `usage`, `net`, `heap`, and `page`.
2. Repeat every command several times; the device must not reboot, assert, or
   trigger the task watchdog.
3. `page 1` and `page 2` move the screen and the UI continues to refresh.
4. `net refresh` works after entering Weather or Usage; on Status it reports
   that the worker is stopped.
5. Leave the device idle for 10 minutes with the monitor attached; it must not
   restart.
6. A merged image flashed from `0x0` writes only the bootloader, partition
   table, and application. The NVS partition that holds Wi-Fi profiles and
   settings is not written, so stored data normally survives; it is lost only if
   the partition table itself changes or NVS is erased explicitly.

A successful build is not a device test. Report `Build`, `Host tests`,
`Device tests`, and `Unverified` separately.
