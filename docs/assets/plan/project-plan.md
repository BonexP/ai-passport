<p align="right">
  <a href="project-plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Project Plan: All-in-One Firmware for FoloToy AI Passport

Scope: custom firmware built on the `FoloToy/ai-passport` baseline (ESP-IDF 5.5.3).
Status: P1 in progress. Storage, boot sweep, status page, preset seeding and SoftAP provisioning are implemented and pass the host tests and the firmware build; provisioning has not been measured on hardware.
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
| Radio | Wi-Fi 2.4 GHz STA and NimBLE share the chip, but not the RAM budget | Networking (STA) and provisioning (SoftAP) are separate modes and never run together, enforced by `wifi_radio_owner`; provisioning never enables Bluetooth |
| Battery | CW2017 fuel gauge | Continuous Wi-Fi polling needs a sleep policy |

Upstream's own rule (`AI_HARDWARE_DEVELOPMENT_GUIDE.md`) requires rechecking the 24 KB LVGL
pool, 9.6 KB LCD DMA, I2S DMA, the 96 KB recording buffer, radio stacks, task stacks, total
free heap and largest free block whenever network, TLS, audio or assets are added. The
baseline explicitly anticipates network features and supplies the accounting checklist.

## 3. Baseline behaviour

`main/demo_wifi.c` (the upstream scan-only demo, deleted in this repo) scans for access points only. It does not associate, does not store
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
   of about 10 seconds; if all fail, stay on the status page with the reason shown and wait for
   a long press on UP to enter provisioning.
3. On disconnect, re-run the whole scan instead of retrying only the last SSID, so the device
   follows the user from home to office.
4. Provisioning uses SoftAP, with **no Bluetooth and no companion mini-program**: the board
   raises an open hotspot (no password, one client, channel 1) and serves a minimal web form,
   the phone's browser submits SSID and password, and the device writes NVS and reboots without
   an APSTA pre-check; the boot sweep verifies the credentials. No captive portal: three buttons and a small screen are enough
   to show the hotspot name and address, while captive-portal probing differs enough across
   phones that verifying it costs far more than it returns.
5. Preset credentials seed the first connection only; see below.

The state machine is pure logic and must be extracted into host-testable code, per upstream
policy. This is the same pattern the `xiaonuo-jianlu` reference application uses.

**Preset credentials: they solve the first connection, not network switching.** The firmware
may carry personal networks, up to `WIFI_PROFILE_MAX` (8):

- On boot the profile store is loaded; **when and only when it is empty**, the presets are
  written into it (`add` then `save`). From then on they are ordinary records that take part in
  the normal sweep. `add` promotes the first entry to active on an empty store, so the first
  preset is tried first.
- A non-empty store means nothing happens. Deleting every network therefore means "factory
  reset", and the next boot re-seeds the presets. That is deliberate: it avoids an
  "already-seeded" flag, the exact kind of field that was removed on 2026-10-29.
- **A preset never merges with an existing record.** Seeding requires an empty store, so there
  is no existing record to collide with. The only possible collision is two lines of the same
  preset file naming one SSID; the first wins and the later one is skipped.
- A single invalid entry (empty SSID, more than 32 bytes, a password over 64, a display name
  over 23) skips just that entry rather than leaving the device with no networks at all.

Mechanism: preset values live in `main/wifi_presets.local.h`, which must be listed in
`.gitignore`, and that entry has to be committed before the file is ever created - once a path
has been in the index, it stays in the scan forever. The tracked `main/wifi_presets.h` only
declares the accessor contract; the `__has_include` fallback lives in `main/wifi_presets.c`, so
credentials compile into a single translation unit and neither tests nor the service layer can
see them. Kconfig was rejected because it has no arrays and because `$`, `"` and `\` in a
password need escaping.

Three hard boundaries:

- **Personal.** A preset may only be the developer's own network. Never commit the real
  credentials of an employer, a university, a client, or anyone else, and never commit a
  public network's account just because it is convenient for testing.
- **Never in version control.** The local header is ignored and does not exist on CI, so CI
  builds firmware with no presets at all and those characters never exist in the repository.
- **Not a security boundary.** The strings land in the firmware's `.rodata` as plaintext and
  can be extracted from the device. A preset buys "no provisioning step", not "hidden
  password"; protecting credentials requires NVS or Flash encryption, which this phase does
  not do. Neither documentation nor UI may imply that a preset protects a password.

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
3. Networking (STA) and provisioning (SoftAP) are separate modes and never run together;
   `wifi_radio_owner` enforces this by refusing the second claim. During provisioning the AP,
   DHCP and HTTP server share internal RAM, the tightest window in this project. The 13 / 31
   KiB figures come from an APSTA reference, so they bound this design rather than describe it.
   The resident networking service must be stopped before entering provisioning.
4. After each feature, record free heap and largest free block in the delivery report.

Reference measurements from the `xiaonuo-jianlu` application on identical hardware and IDF
version: about 42 KB heap remaining with the Wi-Fi driver resident, about 58 KB required by
the NimBLE stack, about 33 KB steady-state heap in the audio + Wi-Fi + HTTP + UI combination,
and audio DMA buffers pre-allocated at boot after a freeze caused by late allocation. The
13 / 31 KiB SoftAP figures come from the `phoenixzhc` resource-budget record. Choosing SoftAP
over BLE has a side benefit: the provisioning path never loads NimBLE, so those 58 KB never
press on the same budget.

## 8. Phased plan

| Phase | Deliverable | Acceptance |
| --- | --- | --- |
| P0 | ESP-IDF 5.5.3 on the Linux host; `./tools/validate.sh` green on the untouched baseline | Build and host tests pass, merged image produced |
| P1 | Wi-Fi profile manager: NVS storage, preset seeding, boot iteration, state machine with host tests, SoftAP provisioning, status page | Reconnects and switches profiles automatically; presets seeded on first boot; phone can provision over the web form; host tests pass |
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
5. How provisioning is entered: **settled - a long press on UP from the status page only.**
   Automatic entry was rejected: once a preset expires it would push the user into provisioning
   on every boot, and the status page already shows "no networks" plus the hold-UP hint.
6. Provisioning is **AP-only**, with no APSTA pre-check: a valid form is written and the device
   reboots, and the boot sweep verifies it. A wrong password is only discovered after that
   reboot; in exchange there is one interface to maintain instead of two. The heap headroom
   under AP-only provisioning has not been measured in this repository; see section 7.

## 10. Evidence index

- Baseline capability contract and hardware facts: `README.md`, `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`
- Development rules: `AGENTS.md`, `docs/development/ai-guide.md`
- Environment and build: `docs/development/engineering/environment-setup.md`, `build-and-test.md`
- Wi-Fi implementation: `main/wifi_profile_service.c` (networking), `main/wifi_provision_service.c` (provisioning), `main/page_status.c` (status page)
- Networked application measurements: `docs/reference/noah-1106/xiaonuo-jianlu/README.md`
- SoftAP resource budget: `docs/reference/phoenixzhc/softap-provisioning-and-resource-budget.md`
- Dependency inventory: `components/bsp/idf_component.yml`
- Fork workflow: `docs/fork-guide.md`
