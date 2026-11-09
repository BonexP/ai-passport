<p align="right">
  <a href="wifi-provisioning.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Wi-Fi provisioning

This repository ships its own SoftAP provisioning flow. The upstream Bluetooth
(BLUFI) flow is kept as history at the end of this page.

## What this firmware does

- **Entry**: long press UP on the status page. The page already shows `No networks`
  and a hint line, so the gesture is discoverable without documentation.
- **Hotspot**: open (`WIFI_AUTH_OPEN`), channel 1, one client, named
  `Passport-XXXX` from the last two bytes of the SoftAP MAC.
- **Address**: `192.168.4.1`, printed on the screen. There is no captive portal, so
  the phone must be pointed at `http://192.168.4.1` by hand.
- **Submit**: five fields as `application/x-www-form-urlencoded`: SSID, password, latitude,
  longitude and place name. The last three are optional; latitude and longitude must be given
  together, and leaving both empty means "this network has no location", so the weather page
  shows `Location not set`.
- **Result**: a valid form is written to NVS with the new record marked active, the
  device answers the request, then reboots and the boot sweep connects. There is no
  APSTA pre-check, so a wrong password shows up on the status page after that
  reboot rather than inside the form.
- **Exit**: long press OK, or five idle minutes (`WIFI_PROV_TIMEOUT_MS`). Either way
  the resident networking service is restored.

## Implementation map

| File | Role |
| --- | --- |
| `main/wifi_provision_service.c` | AP netif, `esp_http_server`, both handlers, worker, restart |
| `main/wifi_provision_flow.c` | session state machine; pure logic, host-tested |
| `main/wifi_provision_form.c` | urlencoded body and `Content-Length` parsing |
| `main/wifi_provision_html.c` | static form page, dynamic list page and the result pages |
| `main/wifi_scan_list.c` | dedupe, ordering and truncation of scan results; pure logic, host-tested |
| `main/wifi_radio_owner.c` | one radio owner: networking or provisioning, never both |
| `main/page_status.c` | renders `Setup: <ssid>`, `Open <ip>`, client count |

## Scanning nearby networks (before the AP starts)

The form offers the surrounding networks as suggestions so the SSID rarely has to be typed.
The price is that the scan **has to happen before the AP is up**:

- `esp_wifi_scan_start()` is not supported in AP-only mode; the IDF guide says only station
  or station/AP coexistence mode supports it.
- In APSTA mode a scan drags the SoftAP onto the channel the station is hopping through
  (ESP32-C3 datasheet), which can drop the phone that is filling in the form.

So the startup sequence inserts `set_mode(STA) -> start -> scan -> stop` between
`esp_wifi_init` and `esp_wifi_set_mode(WIFI_MODE_AP)`. At that moment the hotspot does not
exist yet, so no phone can be dropped.

- **Never fatal**: any failure leaves the list empty, the form falls back to typing the SSID,
  and provisioning still works.
- At most 16 entries; one row per SSID (the strongest advertisement wins); sorted by RSSI;
  hidden networks (no SSID in the beacon) are skipped.
- Cost: roughly two extra seconds when entering provisioning, about 640 B for the list and
  1.6 KB for the driver records, a page buffer raised from 1 KB to 4 KB, and an httpd stack
  raised from 5120 to 6144.
- There is **no rescan button**: AP-only mode has no second scan, so refreshing the list means
  leaving provisioning and entering it again.

## HTTP input boundaries

- `Content-Length` must be present and numeric. A value over `WIFI_PROV_BODY_MAX`
  (1024) is rejected before a single byte of the body is read.
- The body is received in a bounded loop with at most two timeout retries. A short
  body is a failure, never a partial parse.
- A malformed form returns an error page and writes nothing to NVS.
- A full profile library fails the save without dropping an existing network:
  losing a working network is worse than not saving this one.
- Coordinates are parsed as decimal degrees and stored as microdegrees. Out-of-range values
  (+/-90, +/-180), non-numbers, or only one of the pair return an error page and write nothing.
  A place name without coordinates is also an error: the name cannot stand on its own.

## Teardown order

`httpd_stop()` first, because it joins the httpd task; then unregister the Wi-Fi and
IP handlers; then stop and deinit the driver; then destroy the netif; then close
NVS; then release the radio claim. Reversing any of these leaves a socket or a
callback pointing at memory that has already been freed.

## Acceptance

Automated: `./tools/validate.sh --static` covers the state machine, both parsers,
the page builders and the service lifecycle, including a per-step rollback of the
thirteen start steps and both HTTP handlers.

Measured on hardware on 2026-10-31 (developer report): flashing; the hotspot appearing and
being joined from a phone browser; the form served at `192.168.4.1` and submitted; the device
rebooting and then joining the submitted network; every long-press menu page.

Still unmeasured, therefore `Unverified`:

- hotspot visibility and the `Passport-XXXX` name on the other mobile platform, plus the
  wording of the system "no internet" warning;
- the new latitude/longitude and place fields: submitting them from a phone and seeing the
  weather page use that location after the reboot;
- `WIFI_EVENT_AP_STACONNECTED` and `IP_EVENT_AP_STAIPASSIGNED` both firing, and only the
  latter moving the client count;
- free heap and largest free block before start, after the AP is up, after httpd
  starts, and after the phone joins;
- the five-minute timeout handing the radio back to the resident service;
- ten entry/exit cycles without leaking sockets, tasks or heap;
- whether the pre-scan really needs no STA netif (this implementation treats scanning as a
  driver-level operation; a failure degrades to "no list");
- the scan itself landing in the two-second range;
- how phone browsers render the `<datalist>` (iOS Safari and Android Chrome differ).

## Upstream Bluetooth reference (historical)

The upstream project provisions over BLE with BLUFI in the
[`demo/blufi-provisioning` branch](https://github.com/FoloToy/ai-passport/tree/demo/blufi-provisioning).
This repository does not use it: it costs a NimBLE stack, needs a phone app or mini
program, and the three-button screen can already show a hotspot name and address.

### Mini program name

The companion mini program's exact name is given in the
[Chinese version](wifi-provisioning.zh_CN.md#mini-program-name); keep it unchanged in
instructions rather than inventing a translated search name. It is the mini program's
name, not the device's BLE advertising name; the reference firmware advertises as
`BLUFI_FoloPassport`.

### Code to inspect

- [`main/demo_blufi.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi.c)
- [`main/demo_blufi_security.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_blufi_security.c)
- [`main/demo_radio.c`](https://github.com/FoloToy/ai-passport/blob/demo/blufi-provisioning/main/demo_radio.c)

The NVS, `esp_netif` and default event loop initialisation in `demo_radio.c` is worth
reading even for a SoftAP flow, because those services are shared by both.
