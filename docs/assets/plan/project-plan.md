<p align="right">
  <a href="project-plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Project Plan: All-in-One Firmware for FoloToy AI Passport

Scope: custom firmware built on the `FoloToy/ai-passport` baseline (ESP-IDF 5.5.3).
Status: P1 complete. Storage, boot sweep, status page, preset seeding and SoftAP provisioning are implemented and pass the host tests and the firmware build. The device test on 2026-10-31 covered flashing, provisioning, joining the submitted network and every long-press menu page; three provisioning items remain unmeasured, see `task-breakdown.md`. P2 is in progress: the compile-time config, per-profile coordinates and place name, SNTP, the HTTPS transport, the weather page and the offline snapshot are implemented with host tests; the weather page and the new provisioning fields have not been measured on hardware. P5 (the AMD Radeon Cloud usage
page, section 4.5) is in progress: the transport, the compile-time key, the usage parser and the
display logic are implemented with host tests; the usage snapshot, the multi-source worker, the
page itself and every device measurement remain. It is ordered before P3.
Research date: 2026-10-27, against upstream `main` at `006c227`. AMD Radeon Cloud API research: 2026-11-10, against documentation last updated 2026-11-03.

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
5. AMD Radeon Cloud usage and daily quota for the developer's own API key.

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

1. Store N profiles in NVS as `{name, ssid, password, location}` plus an `active_index`. Only *which*
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
6. Each profile carries a latitude/longitude pair and a place name (record v3; v2 stays
   readable). Weather follows whichever network is connected; a network without coordinates
   shows `Location not set`. The provisioning form keeps coordinates optional but requires the
   pair to be filled together.

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

Mechanism: preset values and the quote watchlist live together in
`main/app_config.local.h`, which must be listed in `.gitignore`, and that entry has to be
committed before the file is ever created - once a path has been in the index, it stays in the
scan forever. The tracked `main/app_config.h` only declares the accessor contract; the
`__has_include` fallback lives in `main/app_config.c`, so credentials compile into a single
translation unit and neither tests nor the service layer can see them. `wifi_presets` keeps only
the seeding behaviour. Kconfig was rejected because it has no arrays and because `$`, `"` and
`\` in a password need escaping.

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

A single HTTPS GET returning a small JSON document (about 0.5 KB for current conditions),
parsed by the minimal field extractor into a bounded static buffer.

Decided: **Open-Meteo** (`api.open-meteo.com`, no key). The request uses
`current=temperature_2m,weather_code&timezone=auto`; `timezone=auto` makes the returned
observation time local, so no timezone table is needed. The location comes from the active
profile's coordinates. The keyless endpoint avoids building a second provisioning path for a
secret; a keyed free tier (QWeather and similar) would leave the key in NVS and still need
editing and revocation paths.

Cadence: fetch only while the weather page is visible, every 10 minutes after a success, with
exponential backoff from 30 seconds up to 5 minutes after a failure.

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

### 4.5 AMD Radeon Cloud usage and quota: feasible, one authenticated GET

Objective: a product page showing today's Token Factory spend against the daily cap, what is
left of it, when it resets, and today's request, token and error counts for the developer's own
API key.

Every fact below was read on 2026-11-10 from the official documentation (last updated
2026-11-03); the pages are listed in section 10.

| Fact | Consequence for the design |
| --- | --- |
| `GET https://developer.amd.com.cn/radeon/api/v1/usage`, API-key auth, China site only | The gateway prefix `/radeon/api` is part of the address. `/v1/usage` without the prefix is the same endpoint, according to the documentation; the client uses the prefixed form, which is the one the documentation's own curl shows |
| `Authorization: Bearer rc-...` | `net_http_get()` sets a fixed User-Agent and nothing else today. A header list and a status-code out-parameter have to be added before this endpoint can be reached at all; this is the only interface change the feature forces on existing code |
| `daily_cost_limit_usd`, `daily_cost_used_usd`, `daily_cost_remaining_usd`, `daily_reset_at`, `daily_reset_timezone`, `today.{requests,errors,total_tokens,prompt_tokens,completion_tokens,cost,last_request_at}`, `last_30_days.{requests,total_tokens,cost}`, `all_time.*`, `by_model[]`, `recent[]` | Everything the page shows sits at depth 0 or 1, inside `json_field`'s existing limits (4 segments, 64-byte path). `by_model` and `recent` are arrays, and the extractor treats an array as an opaque value, so neither is reachable without a second parser. v1 shows neither |
| The `_usd` suffixes are Token Factory points, not currency. The documentation states this twice, and the values are unrelated to account credits | The page labels the unit `pts` and never prints `$`. A dollar sign would misstate the balance, which is the one thing this page exists to report |
| `daily_cost_limit_usd` is per account and changes over time; `rpm_limit` is per key | Never compile a cap in. Render the cap from the last response, and treat a missing cap as unknown rather than zero |
| The daily period is the calendar day in Beijing time; `daily_reset_at` carries the exact instant with a `+08:00` offset | Show the clock time taken from `daily_reset_at`. A countdown would need ISO-8601-with-offset to epoch arithmetic and buys nothing when the reset is always 00:00 |
| `401` covers both a missing header and a rejected key, `429` carries `Retry-After`, `502` and `503` are transient, and errors arrive in two different envelopes (`error` or `detail`) | The page tells "key rejected" apart from "rate limited" and from "service unavailable". That distinction is the whole answer to "why is the card empty", and a single generic failure code cannot carry it |
| `include_recent` defaults to `false` | Omit the parameter. `recent` is the only unbounded part of the document, and the screen has no room for request-level detail |
| Response size: the documented example measures about 0.6 KB compact and 0.8 KB indented, `by_model` included | Fits the existing 4 KB shared body buffer with a wide margin. Truncation is a parse failure with its own message; partial numbers are never displayed |

Decision: one authenticated GET per fetch, parsed by the existing field extractor into a second
record on the `app_data` bus, cached in NVS under its own versioned snapshot, and rendered on a
new page in the product ring: status, weather, usage, status.

Non-goals for v1, so the slice stays reviewable:

- No per-model breakdown. `by_model` is an array, the extractor does not descend into arrays,
  and eight models would not fit the panel either.
- No `recent` request list and no `all_time` totals.
- No countdown, no chart, no alert when the quota runs low. The page reports numbers; nothing
  acts on them.
- No key editing on the device. The key is compile-time, exactly like the Wi-Fi presets in
  section 4.1; rotating it means rebuilding and reflashing.

Cadence: the 10-minute interval the weather page already uses, unchanged. Quota is metered
server-side and only moves when the account makes model calls, which this device does not do;
the page is a monitor, not a meter. Six requests per hour is far below both documented limits
(30 rpm per key, 20 rpm per account).

**Key handling.** The key is a secret and follows the preset rules without exception:

- `APP_CONFIG_AMD_KEY` lives in `main/app_config.local.h`, which git ignores. The tracked
  `app_config.h` declares only an accessor returning `const char *` (NULL when the section is
  absent), and `app_config.c` stays the only translation unit that includes the local header.
- The key lands in `.rodata` in plaintext and can be extracted from a flash dump. This is not a
  security boundary: it keeps a key out of git, not off the device. Neither the code nor the UI
  may imply otherwise.
- A CI build carries no key. The page then renders "Key not configured" and opens no socket.
- The key never appears in a URL, so request URLs stay safe to log; it appears in exactly one
  header. No log line may print that header, and `net_http` keeps its existing "no logging"
  property.
- `tools/check_repo.py` gains an `rc-` key-shape pattern in `SECRET_PATTERNS`, so a key committed
  anywhere in the tree fails the static gate. The pattern requires 20 or more ASCII alphanumerics
  after the prefix, so the documentation's masked example and the template's placeholder do not
  match it.

**What the network layer has to change.** The weather slice is single-source by construction:
one schedule, one visibility flag, one refresh flag, one snapshot, one state record. A second
source needs four splits, all host-testable:

1. `net_http_get_ex()` takes a header list and an optional status-code out-parameter;
   `net_http_get()` becomes a wrapper that passes neither, so the weather path and its existing
   tests stay untouched. The HTTP stub must record headers for this to be testable at all; today
   it discards them.
2. `net_schedule` itself needs no change, because the struct is already self-contained and a
   second instance is enough. The worker changes instead: per-source visibility, per-source
   refresh, and at most one fetch per loop iteration, so the 4-second stop budget still covers a
   single in-flight TLS session.
3. `app_data` gains `usage_state_t` beside `weather_state_t`. One bus, one critical section, one
   `seq` per record; the UI redraws when either sequence moves.
4. `net_snapshot_store` gains a generic blob save and load, so each feature owns its own versioned
   layout and NVS key (`weather`, `usage`) and the two weather functions become wrappers. The
   alternative, one store function pair per feature, grows without bound, and P3 adds a third
   record.

`main.c` needs the same treatment: its ring transition is a two-page special case today
(status to weather, weather to status) and must become one `page_switch(page_id_t)` before a
third page turns six transitions into six branches.

**Display.** 240x320 portrait, the existing product shell, no new visual language:

| Row | Content | Source field |
| --- | --- | --- |
| Header | `AI PASSPORT`, battery | existing `ui_product_header` |
| Title | `AMD Cloud` | static |
| Main card | remaining points at 2 decimals; below 1 point keep 4 decimals, so `0.0032` does not read as `0.00` | `daily_cost_remaining_usd` |
| Card detail | `1.84 / 10.00 used` plus a 2 px used-over-limit bar | `daily_cost_used_usd`, `daily_cost_limit_usd` |
| Line | `Reset 00:00` | `daily_reset_at` |
| Line | `Today 142 req  88.2k tok  3 err` | `today.*` |
| Line | `30d 39.77 pts` | `last_30_days.cost` |
| Footer | `OK refresh   UP/DOWN page` | existing convention |

Points are stored as fixed-point integers at 1e-4 points, matching the four decimals the API
returns, so no floating-point value survives the parser; the range, about 214,000 points, covers
every plausible cap. Token counts are abbreviated with integer arithmetic (`88214` becomes
`88.2k`), because the device never formats a float. `usage_format_points()` and the used-over-limit
percentage are pure functions with host tests.

**Failure display.** The last good numbers stay on screen with their timestamp, exactly as the
weather page behaves. A failure republishes only the status field, so a rejected key does not
erase the quota it was showing. The status line distinguishes: no key configured, key rejected
(401, 403), rate limited (429), service unavailable (5xx or transport), truncated response.
`usage_view_state()` and its table are host-tested; only the glyphs on the panel stay unverified
until the device test.

**Memory cost.** No new task, stack or buffer: the same worker, the same 4 KB body, one TLS
session at a time, fetches strictly sequential. The additions are one `usage_state_t` (about
72 bytes) on the bus and the page's LVGL objects. The heap watermark is recorded with the P5
device test like every other feature.

## 5. Architecture: device-only

```
[AI Passport]
  Wi-Fi profile manager (NVS + state machine)
        |
  worker task: HTTP/TLS client, bounded static buffers, JSON parse
        |
  data model (weather, usage, quotes, endpoint status) in RAM, last snapshot in NVS
        |
  LVGL UI task (bsp_lvgl_lock held for every access)
```

Design rules that follow from having no hub:

- Credentials, if any, live in a NVS record or in a git-ignored local header, never in a
  committed file and never in an image published as a release.
- Each network feature owns a small bounded buffer allocated at boot when the heap is healthy.
- No feature may block the LVGL task or a button callback.
- Offline behaviour is explicit: show the last snapshot from NVS with a visible stale marker
  and the timestamp of the snapshot. A snapshot carries no network identity, so it is shown
  only when there is no connection or the current profile has a location; a connected network
  without a location is the terminal `Location not set` state, never another network's data.
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
5. A second network source adds no task, stack or buffer. The worker fetches at most one source
   per iteration, so the 4-second stop budget keeps covering exactly one in-flight request and
   the TLS peak stays single-session.

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
| P2 | Compile-time config, per-profile coordinates, SNTP, HTTPS client with bounded buffers, minimal JSON, weather page and offline snapshot; endpoint health page pending | Shows stale snapshot when offline |
| P3 | Stock page with eight rows and a host-tested polling scheduler | Correct data during trading hours |
| P4 | Polish: CJK font, sleep policy, battery readout, offline queue | Overnight battery measurement |
| P5 | AMD Radeon Cloud usage page: header injection, compile-time key, usage parser and formatters, versioned snapshot, multi-source worker, third ring page | The numbers match the console's Credits & Usage page; a missing or rejected key says so instead of showing an empty card; host tests cover parsing, formatting, display state and the snapshot |

P5 was added after the phases above, so its number is out of implementation order. It is
scheduled before P3: both need the same multi-source split, and P5 is the smaller slice, so it
proves the refactor on one endpoint before the stock page leans on it.

Each phase is developed on its own topic branch and merged into the long-lived integration
branch. Delivery reports follow the upstream four-part format: Build, Host tests, Device tests,
Unverified.

## 9. Open questions

1. ~~Weather provider~~ Settled: **Open-Meteo** (no key), location from the active profile's
   coordinates (2026-11-01).
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
7. SNTP is settled: TLS certificate validity needs a trustworthy clock, so the device syncs time
   after joining Wi-Fi. With an invalid clock it refuses to request rather than skipping
   certificate checks. Snapshots are written on content change or every 30 minutes, so the
   24 KB NVS partition is not worn out.
8. Config self-check is settled: boot validates the local config and logs each problem (Wi-Fi
   fields, coordinates, place, watchlist, duplicates, count limits). Weather values are bounded
   to +/-100 C and WMO 0-99, and a 2xx response that declares a non-JSON Content-Type is treated
   as a failure (a missing header falls through to the parser).
9. Does `GET /v1/usage` count against the per-minute allowance? The documentation says listing
   models does and does not say so for this endpoint. Either answer leaves six requests per hour
   far below every documented limit, so the open question changes documentation, not behaviour.
10. Whether the TLS chain for `developer.amd.com.cn` validates against the ESP-IDF certificate
    bundle is unverified until the device test. If it does not, the fix is a pinned CA
    certificate, never a skipped check.
11. Key rotation: rebuild and reflash is accepted for a single personal key. If that becomes a
    nuisance, the next slice is an NVS record plus a provisioning field, which then needs the
    same treatment as a Wi-Fi password.
12. A build without a key still shows the usage page, reading "Key not configured". Hiding it
    would make the ring order depend on the build, and the weather page already sets the
    precedent with "Location not set".

## 10. Evidence index

- Baseline capability contract and hardware facts: `README.md`, `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`
- Development rules: `AGENTS.md`, `docs/development/ai-guide.md`
- Environment and build: `docs/development/engineering/environment-setup.md`, `build-and-test.md`
- Wi-Fi implementation: `main/wifi_profile_service.c` (networking), `main/wifi_provision_service.c` (provisioning), `main/page_status.c` (status page)
- Networked application measurements: `docs/reference/noah-1106/xiaonuo-jianlu/README.md`
- SoftAP resource budget: `docs/reference/phoenixzhc/softap-provisioning-and-resource-budget.md`
- Dependency inventory: `components/bsp/idf_component.yml`
- Fork workflow: `docs/fork-guide.md`
- AMD Radeon Cloud API, read 2026-11-10 against documentation last updated 2026-11-03:
  `https://amd-aim.github.io/radeon-cloud-docs/api/usage/` (endpoint, fields, points-versus-USD),
  `.../api/overview/` (base URL, gateway prefix, China-only), `.../api/rate-limits/` (per-key and
  per-account limits, reset at 00:00 Beijing time), `.../api/errors/` (401, 429, 502, two error
  envelopes)
