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
- **Submit**: SSID and password as `application/x-www-form-urlencoded`.
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
| `main/wifi_provision_html.c` | static form page and the result pages |
| `main/wifi_radio_owner.c` | one radio owner: networking or provisioning, never both |
| `main/page_status.c` | renders `Setup: <ssid>`, `Open <ip>`, client count |

## HTTP input boundaries

- `Content-Length` must be present and numeric. A value over `WIFI_PROV_BODY_MAX`
  (1024) is rejected before a single byte of the body is read.
- The body is received in a bounded loop with at most two timeout retries. A short
  body is a failure, never a partial parse.
- A malformed form returns an error page and writes nothing to NVS.
- A full profile library fails the save without dropping an existing network:
  losing a working network is worse than not saving this one.

## Teardown order

`httpd_stop()` first, because it joins the httpd task; then unregister the Wi-Fi and
IP handlers; then stop and deinit the driver; then destroy the netif; then close
NVS; then release the radio claim. Reversing any of these leaves a socket or a
callback pointing at memory that has already been freed.

## Acceptance

Automated: `./tools/validate.sh --static` covers the state machine, both parsers,
the page builders and the service lifecycle, including a per-step rollback of the
thirteen start steps and both HTTP handlers.

None of the following has been measured on hardware; all of it belongs under
`Unverified`:

- hotspot visibility and the `Passport-XXXX` name on Android and iOS;
- opening `192.168.4.1` by hand, including the "no internet" warning;
- `WIFI_EVENT_AP_STACONNECTED` and `IP_EVENT_AP_STAIPASSIGNED` both appearing;
- free heap and largest free block before start, after the AP is up, after httpd
  starts, and after the phone joins;
- connecting to the submitted network after the reboot;
- the five-minute timeout handing the radio back to the resident service;
- ten entry/exit cycles without leaking sockets, tasks or heap.

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
