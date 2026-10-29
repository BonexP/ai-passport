<p align="right">
  <a href="project-plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Project Plan: All-in-One Firmware for FoloToy AI Passport

Scope: custom firmware built on the `FoloToy/ai-passport` baseline (ESP-IDF 5.5.3).
Status: planning. No application code has been written yet.
Research date: 2026-10-27, against upstream `main` at `006c227`.

Bottom line: the feature set is feasible without an external helper service. The only
hard constraint is RAM: the board has no PSRAM, so features compete for roughly 320 KB
of internal SRAM instead of disk or network budget. That constraint decides ordering and
concurrency, not feasibility.

## 1. Objective

One firmware that covers:

1. Multi-location Wi-Fi profiles with automatic roaming between them (home, office, other).
2. Stock quotes for a fixed watchlist.
3. Weather for the current location.
4. Local network endpoint health checks and mDNS service discovery.

The device is standalone. There is no LAN hub, no companion server, and no dependency on
another machine being powered on. Every feature must either work from the device alone or
degrade to a clearly displayed offline state.

## 2. Hardware envelope

Sources: `components/bsp/include/bsp_pins.h`, `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`,
`README.md` capability table.

| Item | Fact | Consequence |
| --- | --- | --- |
| MCU | ESP32-C3, 8 MB flash, no PSRAM | Everything lives in internal SRAM; Wi-Fi, TLS, LVGL and audio compete |
| Display | ST7789 240x320 portrait, RGB565, LVGL 9, 24 KB pool plus one 9.6 KB DMA buffer | One quote screen fits; no double buffering, no large images |
| Buttons | UP / DOWN / OK on a shared GPIO0 ADC ladder | Callbacks must not block; network work goes to worker tasks |
| Audio | ES8311 I2S full duplex, demo recording buffer about 96 KB | Audio and full networking cannot both stay resident |
| Radio | Wi-Fi 2.4 GHz STA and NimBLE share the chip, but not the RAM budget | Provisioning and networking are separate modes |
| Battery | CW2017 fuel gauge | Continuous Wi-Fi polling needs a sleep policy |

Upstream's own rule (`AI_HARDWARE_DEVELOPMENT_GUIDE.md`) requires rechecking the 24 KB LVGL
pool, 9.6 KB LCD DMA, I2S DMA, the 96 KB recording buffer, radio stacks, task stacks, total
free heap and largest free block whenever network, TLS, audio or assets are added. The
baseline explicitly anticipates network features and supplies the accounting checklist.

## 3. Baseline behaviour

`main/demo_wifi.c` scans for access points only. It does not associate, does not store
credentials and does not perform any HTTP request. A search across `main/` and
`components/` finds no use of `esp_http_client`, `esp_tls` or a JSON parser.

Networking, storage of credentials, HTTP, TLS and JSON parsing are all our work. The good
news: `esp_http_client`, `esp-tls` (mbedTLS), `nvs_flash`, `esp_ping`, `mdns` and `json`
are all built-in ESP-IDF 5.5.3 components, so no new managed dependency is required.

## 4. Feature assessment

### 4.1 Multi-location Wi-Fi profiles: feasible, lowest risk

Mechanism, using only built-in APIs:

1. Store N profiles in NVS as `{name, ssid, password}` plus an `active_index`. Only *which*
   network succeeded is kept, never *when*: the board has no RTC, so a timestamp would not
   survive a reboot, and `active_index` is the only input the state machine needs.
2. On boot, try `active_index` first; on failure iterate the list with a per-profile timeout
   of about 10 seconds; if all fail, enter a provisioning state.
3. On disconnect, re-run the whole scan instead of retrying only the last SSID, so the device
   follows the user from home to office.
4. Provisioning uses BLE (BLUFI pattern) and writes NVS, then reboots into a pure Wi-Fi path.

The state machine is pure logic and must be extracted into host-testable code, per upstream
policy. This is the same pattern the `xiaonuo-jianlu` reference application uses.

### 4.2 Weather: feasible, simplest network feature

A single HTTPS GET returning a small JSON document (well under 10 KB), parsed with the
built-in JSON component into a bounded static buffer. Poll interval of five minutes or more.

Open point: which provider. A free tier with an API key (for example QWeather) works, but
the key then lives in NVS on the device. A keyless public endpoint avoids that but has weaker
stability guarantees. To be decided before implementation.

### 4.3 Stock quotes: feasible, data volume is the constraint

Watchlist: CSI AI Index, AI ETF, Nasdaq 100, S&P 500, NVIDIA, Tesla, spot gold, Micron.

Eight symbols in one or two requests is a few KB of JSON, comfortably inside a static
streaming buffer. Display: a 240x320 portrait screen fits eight rows of
name / price / change percent / change amount / turnover.

Data source options, in preference order:

- **Keyless public quote endpoints.** Typical of Chinese market data providers that return
  compact JSON or delimited text over HTTPS with no authentication. Needs verification of
  response size, encoding and rate limits before committing.
- **Keyed API (iwencai or similar).** Richer data, but the key must be provisioned into NVS
  and never committed. Adds a provisioning path for a secret, which is extra work.

The decision affects only the client layer, so it can be deferred until the display and
polling scheduler are proven.

Polling schedule is pure logic (market hours for A-shares and US equities, backoff outside
trading hours) and must be host-tested.

### 4.4 Local network probe: feasible once narrowed

- ICMP reachability for a small known endpoint list via `esp_ping`. Cheap.
- mDNS browsing for advertised services via the built-in `mdns` component.
- A full /24 TCP port scan is rejected: one TCP probe costs roughly 100 to 300 ms on 2.4 GHz,
  a sweep takes minutes, and the screen cannot present the result usefully. It also competes
  with the audio and TLS memory budget for no product value here.

Defined scope: "are the endpoints I care about reachable, and what services are advertised
on this network". The endpoint list is user-editable in NVS.

## 5. Architecture: device-only

```
[AI Passport]
  Wi-Fi profile manager (NVS + state machine)
        |
  worker task: HTTP/TLS client, bounded static buffers, JSON parse
        |
  data model (quotes, weather, endpoint status) in RAM, last snapshot in NVS
        |
  LVGL UI task (bsp_lvgl_lock held for every access)
```

Design rules that follow from having no hub:

- Credentials, if any, live in NVS, never in the repository and never in a committed header.
- Each network feature owns a small bounded buffer allocated at boot when the heap is healthy.
- No feature may block the LVGL task or a button callback.
- Offline behaviour is explicit: show the last snapshot from NVS with a visible stale marker
  and the timestamp of the snapshot.
- mDNS discovery must fall back to cached direct addresses when a router does not forward
  multicast (RFC 6762 section 8.1). This is a known failure mode recorded in the reference
  application.

## 6. Chinese text

Default Montserrat has no CJK glyphs; UTF-8 compiling successfully does not mean it renders.
Two routes:

- **A (recommended):** generate a GB2312-covering subset (about 7,000 glyphs at 16 px) from
  Noto Sans SC with `lv_font_conv` and commit the generated C file. 8 MB flash has room.
- **B:** collect only the characters the application uses. Smaller, but every new string
  requires regenerating the font.

Route A is chosen: quote names and weather labels are Chinese, and re-generating fonts on
each string change is a recurring tax.

## 7. Memory discipline

1. Pre-allocate LVGL pool, audio DMA (if enabled) and HTTP buffers at boot.
2. All network, storage and audio work happens in worker tasks.
3. BLE provisioning and Wi-Fi networking are separate boot modes; the two stacks never run together.
4. After each feature, record free heap and largest free block in the delivery report.

Reference measurements from the `xiaonuo-jianlu` application on identical hardware and IDF
version: about 42 KB heap remaining with the Wi-Fi driver resident, about 58 KB required by
the NimBLE stack, about 33 KB steady-state heap in the audio + Wi-Fi + HTTP + UI combination,
and audio DMA buffers pre-allocated at boot after a freeze caused by late allocation.

## 8. Phased plan

| Phase | Deliverable | Acceptance |
| --- | --- | --- |
| P0 | ESP-IDF 5.5.3 on the Linux host; `./tools/validate.sh` green on the untouched baseline | Build and host tests pass, merged image produced |
| P1 | Wi-Fi profile manager: NVS storage, boot iteration, state machine with host tests, status page | Reconnects and switches profiles automatically; host tests pass |
| P2 | HTTP/TLS client with bounded buffers, minimal JSON, weather page, endpoint health page | Shows stale snapshot when offline |
| P3 | Stock page with eight rows and a host-tested polling scheduler | Correct data during trading hours |
| P4 | Polish: CJK font, sleep policy, battery readout, offline queue | Overnight battery measurement |

Each phase is developed on its own topic branch and merged into the long-lived integration
branch. Delivery reports follow the upstream four-part format: Build, Host tests, Device tests,
Unverified.

## 9. Open questions

1. Weather provider: keyed free tier in NVS, or keyless public endpoint?
2. Stock provider: keyless endpoint sufficient, or does the keyed API justify the extra
   provisioning work?
3. Audio: in scope this cycle or deferred to a later phase after the network stack is stable?
4. Endpoint probe list and whether mDNS discovery is enough without explicit ICMP targets.

## 10. Evidence index

- Baseline capability contract and hardware facts: `README.md`, `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`
- Development rules: `AGENTS.md`, `docs/development/ai-guide.md`
- Environment and build: `docs/development/engineering/environment-setup.md`, `build-and-test.md`
- Baseline Wi-Fi behaviour: `main/demo_wifi.c` (scan only)
- Networked application measurements: `docs/reference/noah-1106/xiaonuo-jianlu/README.md`
- SoftAP resource budget: `docs/reference/phoenixzhc/softap-provisioning-and-resource-budget.md`
- Dependency inventory: `components/bsp/idf_component.yml`
- Fork workflow: `docs/fork-guide.md`
