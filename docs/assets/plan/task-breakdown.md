<p align="right">
  <a href="task-breakdown.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Task Breakdown

Planning only. No application code exists yet. This file decomposes the phases in
`project-plan.md` into atomic tasks with verifiable acceptance criteria, so that each task can
be taken on a topic branch and reviewed on its own.

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
| Firmware build on untouched baseline | pending | `./tools/validate.sh --firmware` has not been run yet |

The remaining P0 item is the first firmware build. It must succeed before any application
code is committed, so that later build failures can be attributed to our changes.

## 3. P1: Wi-Fi profile manager

| ID | Task | Acceptance |
| --- | --- | --- |
| P1.1 | Define the profile record and NVS schema: name, ssid, password, last_ok_time, active index | Schema documented in a header comment; round-trip unit test on the host with a stub key-value store |
| P1.2 | Profile store module, host-testable, no ESP-IDF calls in the logic layer | Add, update, delete, list, select-active all covered by host tests |
| P1.3 | Boot connection state machine: try active, then iterate with per-profile timeout, then provisioning state | Host tests cover all-success, mid-list success, all-fail, and timeout paths |
| P1.4 | Disconnect handling: re-run the full iteration instead of retrying the last SSID | Host test for the transition; device behaviour listed as Unverified until measured |
| P1.5 | Wi-Fi worker task wiring to the real ESP-IDF APIs | Build passes; runtime reading of free heap and largest free block recorded |
| P1.6 | Provisioning path decision (BLE per `demo/blufi-provisioning` pattern) | Decision recorded; implementation is a separate task with its own review |
| P1.7 | Status page UI, redesigned rather than copied from a demo | Own layout and key handling described in the delivery report; screen result marked Unverified until seen on the device |

Notes: the state machine from P1.3 is the piece upstream policy most wants under host test.
Provisioning (P1.6) is deliberately separated because it introduces the BLE stack and the
"two stacks never coexist" constraint.

## 4. P2: network client, weather, endpoint health

| ID | Task | Acceptance |
| --- | --- | --- |
| P2.1 | Bounded HTTP client wrapper: static buffer, timeout, no heap allocation per request | Host tests for buffer boundaries and truncation handling; request path is device-only and marked Unverified |
| P2.2 | Minimal JSON extraction for known-shape responses | Host tests using captured sample payloads; no dynamic tree, no unbounded recursion |
| P2.3 | Weather provider decision, recorded with evidence | Provider, endpoint, key handling and rate limit documented; unverified parts listed |
| P2.4 | Weather page with timestamp and stale marker | Own UI; stale snapshot shown when the request fails |
| P2.5 | Endpoint health checks over ICMP for a user-editable list | List persisted in NVS; host test for schedule logic |
| P2.6 | mDNS browsing with unicast fallback | Fallback path documented; device result Unverified |
| P2.7 | Offline snapshot persistence | Snapshot written after each successful fetch; boot with a stale snapshot renders the stale marker |

## 5. P3: stock quotes

| ID | Task | Acceptance |
| --- | --- | --- |
| P3.1 | Watchlist record: eight symbols with display names | Host test for ordering and label handling |
| P3.2 | Quote provider decision, recorded with evidence | Response shape, size, encoding and rate limit documented; keyless path preferred |
| P3.3 | Quote parser and data model | Host tests against captured payloads, including malformed input |
| P3.4 | Polling scheduler: trading hours, backoff, no polling while the screen is off | Host tests for schedule transitions across both markets |
| P3.5 | Quote page: eight rows, name, price, change percent, change amount, turnover | Own UI; sign formatting covered by host test; rendering marked Unverified until seen |

## 6. P4: polish

| ID | Task | Acceptance |
| --- | --- | --- |
| P4.1 | CJK subset font generation pipeline, reproducible | Generation command recorded; glyph coverage checklist in the delivery report; on-screen rendering verified on device |
| P4.2 | Sleep policy: screen off, modem sleep, wake sources | Measured current draw recorded as a device result |
| P4.3 | Battery readout from the CW2017 gauge | Reading compared against a reference measurement; accuracy limits stated |
| P4.4 | Memory accounting pass across all features | Free heap and largest free block recorded per feature combination |

## 7. Open decisions blocking work

These must be settled before the tasks that depend on them, and each is recorded as a decision
note rather than left implicit:

1. Weather provider: keyed free tier in NVS or keyless endpoint (blocks P2.3).
2. Stock provider: keyless endpoint or keyed API (blocks P3.2).
3. Provisioning transport for P1.6: BLE now, or defer and edit profiles by re-flashing.
4. Audio: excluded this cycle; revisiting it invalidates the current memory budget.
5. Endpoint probe list contents for P2.5.

## 8. Suggested order

P0 firmware build, then P1.1 to P1.5 as one vertical slice (store, state machine, worker), then
P2.1 and P2.2 (shared client and parser), then P2.3 and P2.4, then P3, then P1.6 and P1.7
alongside P2 because provisioning and status UI both benefit from a working network path, then
P4 last.

## 9. Portability tasks (conditional)

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
