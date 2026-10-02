# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A **Meshtastic client for the M5Stack Tab5** (ESP32-P4 host + onboard ESP32-C6 BLE
controller over esp-hosted/SDIO). The codebase is the v2 rebuild of
`../_archive/Tab5-Meshtastic` (v1) — v1 is kept as **reference, not a base to fork**.

**`PRD.md` is the spec.** It carries requirements, the layered architecture, the UI
design system, the exact Meshtastic GATT contract, and hardware facts that cost real
time to re-derive.

## Build / flash

ESP-IDF v5.4.4 is at `~/esp/esp-idf`, auto-sourced (`idf.py` on PATH).

```bash
# First time / after sdkconfig changes:
source ~/esp/esp-idf/export.sh
idf.py set-target esp32p4

# Normal build:
idf.py build

# Flash (battery MUST be plugged in before this):
idf.py -p /dev/ttyACM0 flash monitor
```

**Always `git commit` before every build.** The only real test is a cold battery
boot — serial resets the P4, so any bad change must be `git revert`-able.

Key stack pinned in `main/idf_component.yml`:
- `espressif/esp_hosted: 1.4.0` + `espressif/esp_wifi_remote: 0.8.5` (C6 BLE via SDIO)
- `m5_tab5_component` (local, `../components/m5_tab5_component`)
- `lvgl/lvgl: 9.5.0`, `espressif/esp_lvgl_port: 2.8.0~1`

## Hardware constraints (non-negotiable)

- **Battery must be plugged in to run/flash.** USB alone browns out the Tab5.
- **Attaching serial (`/dev/ttyACM*`) resets the P4.** On-screen diagnostics are the
  primary observability surface (see `diag_t` in `app_state.h`). Port is plug-order
  dependent (ACM0 vs ACM1).
- **Cold boot ≠ warm serial-reset.** Any change to board bring-up / power / boot path
  must be validated on a real cold battery boot.
- **BLE sync is poll-driven, NOT notification-driven.** FromNum GATT notifications
  never traverse the esp-hosted tunnel. ~600 ms poll of FromRadio is the sync engine.
- **FromRadio read callbacks occasionally never fire** over SDIO. Every read needs a
  timeout (~1.5 s) → abandon → re-issue, or the single-in-flight guard wedges.
- **Display is 1280×720 landscape via PPA hardware rotation** in `lcd_tools.cpp`.
  Do NOT use runtime LVGL rotation — it corrupts the render.
- Request **MTU 512** (negotiates to 255); gate first reads on MTU exchange.
  Never use `read_long`. `want_config_id` and retried ToRadio writes need an
  **incrementing** id (radio dedupes by id).

## Architecture

The hard rule: **BLE/transport never touches LVGL; UI never calls transport except
through `app_commands.h`.**

```
main/
  app/          — AppState (mutex-guarded single source of truth), settings
  ble/          — BLE transport (NimBLE central over C6 via esp-hosted)
  transport/    — IMeshTransport interface + UartTransport (RAK3172H via Grove)
  protocol/     — MeshSession (Meshtastic handshake above a transport)
  mesh/         — mesh_proto: pure nanopb encode/decode (no BLE, no LVGL)
  ui/           — LVGL shell, screens, theme.h design tokens
  board/        — lcd_tools, keyboard, battery_monitor, audio, camera
  storage/      — settings_store, message_store (NVS), app_storage (SPIFFS)
  network/      — wifi_service
```

### Thread model

- **BLE/transport task** → publishes into `AppState` via `app_state_*()` calls.
- **LVGL task** → reads immutable snapshot via `app_state_snapshot()`; emits
  commands only through `app_send_text()` / `ble_transport_*()`.
- No other cross-task communication. The connection state machine lives entirely
  in `ble_transport.cpp` (BLE mode) or `MeshSession` (UART mode).

### Transport selection

Set by `settings_store_get()->transport` (0 = BLE, 1 = UART). At boot, `main.cpp`
either starts the full BLE stack (`esp_hosted_init` → `nimble_port_init` →
`ble_transport_start`) or attaches `UartTransport` to `MeshSession` and skips
NimBLE entirely. The UI sends text via `app_send_text()` which routes to the active
transport.

### UART transport

`UartTransport` (Grove port: TX=GPIO53, RX=GPIO54, 115200 baud, UART2) talks to a
RAK3172H via the `SerialFramer` length-prefixed framing protocol. `MeshSession`
owns the Meshtastic handshake (want_config_id, config_complete_id) on top of it.

### Channel management

`main/protocol/channel_url.cpp` encodes/decodes Meshtastic channel URLs (base64url
`MeshChannel` proto, `#` fragment). The channel editor UI lives in
`main/ui/screens/channels_screen.cpp`. QR display and camera import are gated on a
successful CSI sensor init; if the camera fails, those buttons are hidden.

### Wi-Fi and time

`wifi_service` (async scan + saved networks) lives in `main/network/`. Clock
authority: SNTP primary, hardware RTC fallback, displayed in chat timestamps. Time
zone is fixed UTC; locale conversion is done in the UI layer only.

### Launcher interoperability

When the user exits to M5Launcher (`app_suspend()`), `main.cpp` de-inits the C6
(BLE/Wi-Fi), releases SDIO, and calls `esp_restart()` with a magic cookie. The
Launcher 2.8 patch (`patches/launcher-2.8.0-tab5-keyboard-rak.patch`) must be
applied to the Launcher source before flashing. See `docs/LAUNCHER_INTEROP.md`.

### Tests

`tests/` contains host-side headless tests built with CMake (not ESP-IDF). Run with:
```bash
cd tests && cmake -B build && cmake --build build && ./build/tab5_tests
```
These cover chat UI state, channel encode/decode, and message dedup logic. They do
not require hardware.

### UI design tokens

All palette, type scale, and metric constants live in `main/ui/theme.h`. Never
hardcode colors or sizes in screen files — always reference theme tokens.

## Reference material

- `../_archive/Tab5-Meshtastic/` — working v1 (reference only).
- `../M5Tab5-UserDemo/` — known-good esp-hosted/SDIO + BSP baseline.
- `../firmware/` — upstream Meshtastic firmware (BLE server reference:
  `src/nimble/NimbleBluetooth.cpp`, `src/mesh/PhoneAPI.{h,cpp}`).

### Test radio

M5Stack **Unit C6L**, BLE name `Meshtastic_0be4`, FIXED_PIN `123456`.
USB serial cross-check: `meshtastic --port <dev> --info`.
