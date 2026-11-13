<p align="right">
  <a href="task-breakdown.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Task Breakdown

P1.1-P1.10 are implemented and covered by host tests. The device test on 2026-10-31 covered
flashing, provisioning, joining the submitted network and every long-press menu page. Six
provisioning items remain unmeasured: hotspot visibility and the system "no internet" prompt on a
second phone platform, the new latitude/longitude and place fields taking effect, the two AP
events observed separately, the heap watermark at four points while the AP is up, the five-minute
timeout handing the radio back, and resource recovery after ten entry/exit cycles. P2.0 config,
the P2.1 bounded response buffer and `net_http` transport, P2.2 field extraction, the P2.3
Open-Meteo decision, the P2.4 weather page and the P2.7 offline snapshot are implemented with host
tests. P2.5 and P2.6 (endpoint health, mDNS) have not started, and neither the weather path nor
the new provisioning fields have been measured on hardware. P5 (the AMD Radeon Cloud usage page)
is in progress: P5.1 through P5.4 are implemented with host tests and merged into
`feature/my-custom`; P5.5 through P5.7 and the P5.8 device measurement remain. It is ordered
before P3, see section 9. This file decomposes the
phases in `project-plan.md` into atomic tasks with verifiable acceptance criteria, so that each
task can be taken on a topic branch and reviewed on its own.

## 1. Rules that apply to every task

- One topic branch per task, cut from `feature/my-custom`, merged back with `--no-ff`.
- Pure logic (state machines, scheduling, layout math, parsing decisions) is separated from
  ESP-IDF and LVGL so host tests can cover it. A task that adds such logic without a host test
  is incomplete.
- `./tools/validate.sh --static` must pass before every commit.
- Delivery report keeps four lines apart: Build, Host tests, Device tests, Unverified.
- Concrete numbers still to be decided (heap watermark, poll intervals) are recorded when the
  task lands, not guessed in advance.

## 2. P0: environment baseline (done)

| Task | State | Evidence |
| --- | --- | --- |
| Install ESP-IDF 5.5.3 | done | `. export.sh && idf.py --version` reports `ESP-IDF v5.5.3-dirty`, see `development-environment.md` |
| Install repository skills | done | `python3 tools/install_passport_skills.py --check` reports all five readable |
| Static gate on untouched baseline | done | `./tools/validate.sh --static` passes |
| Firmware build on untouched baseline | done | CI `Firmware checks` (workflow_dispatch on `feature/my-custom`) passes, merged-image verification included |

P0 is complete. The baseline build went green before any application code, so later build
failures can be attributed to our changes. The development machine has no ESP-IDF, so builds
go through CI's `firmware-checks.yml`; `./tools/validate.sh --firmware` only applies where a
local toolchain exists.

## 3. P1: Wi-Fi profile manager

| ID | Task | Acceptance |
| --- | --- | --- |
| P1.1 | Define the profile record and NVS schema: name, ssid, password, active index | Schema documented in a header comment; round-trip unit test on the host with a stub key-value store |
| P1.2 | Profile store module, host-testable, no ESP-IDF calls in the logic layer | Add, update, delete, list, select-active all covered by host tests |
| P1.3 | Boot connection state machine: try active, then iterate with per-profile timeout, then provisioning state | Host tests cover all-success, mid-list success, all-fail, and timeout paths |
| P1.4 | Disconnect handling: re-run the full iteration instead of retrying the last SSID | Host test for the transition; device behaviour listed as Unverified until measured |
| P1.5 | Wi-Fi worker task wiring to the real ESP-IDF APIs | Build passes; runtime reading of free heap and largest free block recorded |
| P1.6 | Provisioning path decision: **SoftAP** (on-board hotspot plus a web form), no Bluetooth and no companion mini-program | Done; decision recorded in `project-plan.md` section 4.1, implementation split into P1.9 and P1.10 |
| P1.7 | Status page UI, redesigned rather than copied from a demo | Own layout and key handling described in the delivery report; screen result marked Unverified until seen on the device |
| P1.8 | Preset credential seeding: `main/app_config.local.h` (commit the ignore entry before the file exists) with the `__has_include` fallback in `main/app_config.c`; `wifi_presets` keeps only the seeding behaviour; `add` then `save` when the store is empty | Five host cases: empty-store seeding, non-empty store untouched, zero presets writing no key at all, invalid entries skipped one by one, and a failed save kept in RAM that recovers on the next boot; CI firmware carries no presets |
| P1.9 | SoftAP provisioning service: open hotspot, minimal HTTP form, save then reboot | Form accepts input; malformed input gets an error page and writes nothing; a successful save answers before rebooting; no APSTA pre-check, so a wrong password shows up on the status page after the reboot; no captive portal. Device result Unverified until measured |
| P1.10 | Mode switch and entry point between provisioning and networking: hold UP on the status page to start provisioning, leave with a long press on OK or after five idle minutes, then restore the service | No residue after the switch (driver, netif and event handlers all unregistered); entry gesture and timeout exit described in the delivery report |

Notes: the state machine from P1.3 is the piece upstream policy most wants under host test.
Provisioning (P1.6) is deliberately separated because provisioning and networking are two
mutually exclusive Wi-Fi modes. With SoftAP chosen, implementation no longer pulls in the BLE
stack, but it does pull in AP, DHCP and an HTTP server, so memory stays the main risk in that
slice (see `project-plan.md` sections 7 and 9, item 6).

## 4. P2: network client, weather, endpoint health

| ID | Task | Acceptance |
| --- | --- | --- |
| P2.0 | Compile-time config: `app_config.local.h` (Wi-Fi presets with coordinates and place, quote watchlist) plus the built-in eight symbols | Without a local file presets are empty and the watchlist falls back to the eight defaults; contract tests pass; boot self-check logs each invalid item (fields, coordinates, place, duplicates, count limits) |
| P2.1 | Bounded HTTP client wrapper: static buffer, timeout, no heap allocation per request | Host tests for buffer boundaries and truncation handling; a 2xx response declaring a non-JSON Content-Type is a failure; request path is device-only and marked Unverified |
| P2.2 | Minimal JSON extraction for known-shape responses | Host tests using captured sample payloads; no dynamic tree, no unbounded recursion |
| P2.3 | Weather provider decision, recorded with evidence | Provider, endpoint, key handling and rate limit documented; unverified parts listed |
| P2.4 | Weather page with timestamp and stale marker | Own UI; old data kept with its timestamp when the request fails; temperature +/-100 C and weather code 0-99, out-of-range is a parse failure |
| P2.5 | Endpoint health checks over ICMP for a user-editable list | List persisted in NVS; host test for schedule logic |
| P2.6 | mDNS browsing with unicast fallback | Fallback path documented; device result Unverified |
| P2.7 | Offline snapshot persistence | Snapshot written on content change or every 30 minutes; boot with a stale snapshot shows its saved time and the offline marker |

Progress: P2.0 config, the P2.1 buffer and `net_http` transport, P2.2 extraction, the P2.3
Open-Meteo decision, the P2.4 weather page and the P2.7 offline snapshot are implemented with
host tests. P2.5 and P2.6 (endpoint health, mDNS) have not started, and neither the weather
path nor the new provisioning fields have been measured on hardware.

## 5. P3: stock quotes

| ID | Task | Acceptance |
| --- | --- | --- |
| P3.1 | Watchlist record: eight symbols with display names | Host test for ordering and label handling |
| P3.2 | Quote provider decision, recorded with evidence | Response shape, size, encoding and rate limit documented; keyless path preferred |
| P3.3 | Quote parser and data model | Host tests against captured payloads, including malformed input |
| P3.4 | Polling scheduler: interval polling while the page is visible, failure backoff; session gating deferred to P4 | Backoff and visibility covered by host tests (`net_schedule` already does) |
| P3.5 | Quote page: eight rows, name, price, change percent, change amount, turnover | Own UI; sign formatting covered by host test; rendering marked Unverified until seen |

## 6. P4: polish

| ID | Task | Acceptance |
| --- | --- | --- |
| P4.1 | CJK subset font generation pipeline, reproducible | Generation command recorded; glyph coverage checklist in the delivery report; on-screen rendering verified on device |
| P4.2 | Sleep policy: screen off, modem sleep, wake sources | Measured current draw recorded as a device result |
| P4.3 | Battery readout from the CW2017 gauge (**implemented**: the status and weather pages already show SOC, see `main/page_status.c:65`) | Reading compared against a reference measurement; accuracy limits stated |
| P4.4 | Memory accounting pass across all features | Free heap and largest free block recorded per feature combination |

## 7. P5: AMD Radeon Cloud usage

| ID | Task | Acceptance |
| --- | --- | --- |
| P5.1 | Header-aware GET: `net_http_get_ex()` with a header list and an optional status-code out-parameter, `net_http_get()` reduced to a wrapper, and the HTTP stub recording the headers it was given | Host tests assert the Authorization header is set exactly once on the usage path and not at all on the weather path, and that both call shapes still reach every existing exit branch |
| P5.2 | Compile-time key: `APP_CONFIG_AMD_KEY` in `main/app_config.local.h`, an accessor returning NULL when the section is absent, a boot self-check entry, plus the template and an `rc-` secret pattern in `tools/check_repo.py` | Contract tests cover a present, absent, empty and non-printable key; the template carries no real key; a deliberately planted `rc-` string fails the repository check |
| P5.3 | `usage_source`: URL build that never contains the key, parse into `usage_state_t` at 1e-4 points, formatters for points, token counts and the reset clock | Host tests run the documented payload plus a missing field, `null`, a string where a number belongs, out-of-range values, a document with `by_model` present and a truncated body; the URL test asserts the key string does not appear |
| P5.4 | `usage_view`: display state machine and the status-to-message table | Host tests cover no key, key rejected, rate limited, service unavailable, waiting for time, updating, retrying, and data with a stale status; the formatting table covers 8.1588, 0.0032, 0 and a six-figure cap |
| P5.5 | Usage snapshot: versioned layout, NVS key `usage`, generic blob store API, save policy driven by the day change | Round-trip, version mismatch, length mismatch and day-change cases are host-tested; the existing weather wrappers still pass their own tests unchanged |
| P5.6 | Multi-source worker: per-source schedule, visibility and refresh flags, at most one fetch per loop iteration | Host tests assert one source's failure does not clear the other's backoff, a hidden page never fetches, and the second source waits for the next iteration rather than sharing one |
| P5.7 | Usage page and ring wiring: `PAGE_USAGE`, `ROUTE_REFRESH_USAGE`, `page_switch()` in `main.c`, and its own layout | Ring and router host tests cover the three-page order and the per-page key table; the screen result is Unverified until seen on the device |
| P5.8 | Device measurement and delivery record | Body length, TLS chain result, heap watermark and the numbers against the console's Credits & Usage page are recorded; anything not measured is listed under Unverified |

P5.1 and P5.6 change shared code, so they land first and alone: a regression in the weather path
would otherwise be indistinguishable from a usage bug. Everything after them is additive.

## 8. Open decisions blocking work

These must be settled before the tasks that depend on them, and each is recorded as a decision
note rather than left implicit:

1. ~~Weather provider~~ - settled: **Open-Meteo**, keyless, location from the active profile
   (2026-11-01, see `project-plan.md` section 4.2).
2. Stock provider: keyless endpoint or keyed API (blocks P3.2).
3. ~~Provisioning transport for P1.6~~ - settled: **SoftAP**, no Bluetooth, no mini-program
   (2026-10-29, see `project-plan.md` section 4.1). ~~How provisioning is entered~~ is settled
   too: hold UP on the status page; leave with a long press on OK or after five idle minutes
   (`project-plan.md` section 9, item 5).
4. Audio: excluded this cycle; revisiting it invalidates the current memory budget.
5. Endpoint probe list contents for P2.5.
6. AMD key handling is settled: compile-time in `app_config.local.h`, no device editing
   (`project-plan.md` section 4.5). Whether `GET /v1/usage` counts against the per-minute
   allowance is open and does not block P5, because the 10-minute cadence is far below both
   documented limits.

## 9. Suggested order

P0 firmware build (done), then P1.1 to P1.5 as one vertical slice (store, state machine,
worker). Next P1.8 preset seeding, because it is what first gives the pipeline real data, then
P1.7 status page, the first time the result is visible on the screen. After that P1.9 and P1.10
for provisioning, then P2.1 and P2.2 (shared client and parser), P2.3 and P2.4, then P5, then
P3, and P4 last.

P5 comes before P3 because both need the multi-source split and P5 is the smaller slice: one
endpoint, one resource shape, no trading calendar. Proving the split there keeps a stock-quote
bug from hiding behind a refactor.

## 10. Portability tasks (conditional)

The chosen workflow is: edit on Windows, verify on CI, flash on Windows. Under that workflow the
gate never runs on Windows, so PT.1 to PT.3 below are **not needed**. They are recorded so the work
is scoped if the decision ever changes.

| ID | Task | Status | Acceptance |
| --- | --- | --- | --- |
| PT.1 | Detect a missing host compiler in `validate.sh` and fail with a clear message | not needed | Simulated on Linux with `CC=/nonexistent`; message names the compiler requirement |
| PT.2 | Accept a preinstalled `actionlint` without calling the installer | not needed | Documented path plus a check that prints the fix |
| PT.3 | Replace `install -m 0644` with a portable copy | not needed | Gate passes without GNU `install` |
| PT.4 | Pin line endings with `.gitattributes` | **done** | Fresh Windows checkout yields LF; scripts run without `/usr/bin/env` errors |

PT.4 is done because it is the only hard prerequisite of the Windows-editing workflow: without it a
single checkout on Windows can write CRLF into the working tree and break every shell script.

The reason PT.1 to PT.3 are deferred rather than dropped: they only matter if a Windows-native gate
becomes a requirement, and each is small enough to pick up later without redesign.
