# Current Architecture (Baseline v2)

## Overview

Tab5-Meshtastic v2 is a Meshtastic BLE client running on the M5Stack Tab5
(ESP32-P4 host, ESP32-C6 BLE coprocessor over SDIO/esp-hosted).

The Tab5 is a **client/interface only** — the Meshtastic node is an external
radio (currently only BLE-connected devices such as the M5Stack Unit C6L).

---

## Layer Diagram

```
┌──────────────────────────────────────┐
│  UI (LVGL task, CPU1)                │
│  ui_shell.cpp — renders snapshots,  │
│  emits commands via function calls   │
└────────────────┬─────────────────────┘
                 │ app_state_snapshot()
                 │ ble_transport_*()
┌────────────────▼─────────────────────┐
│  AppState / Settings (app/)          │
│  Single mutex, ring buffers          │
│  settings.cpp → NVS persistence      │
└────────────────┬─────────────────────┘
                 │ app_state_set_*()
┌────────────────▼─────────────────────┐
│  Meshtastic Protocol (mesh/)         │
│  mesh_proto.cpp — pure nanopb        │
│  encode/decode on byte buffers       │
└────────────────┬─────────────────────┘
                 │ raw protobuf bytes
┌────────────────▼─────────────────────┐
│  BLE Transport (ble/)                │
│  ble_transport.cpp — NimBLE central  │
│  GATT, MTU, poll/drain, recovery     │
└──────────────────────────────────────┘
```

---

## BLE Flow

### Connection Sequence
1. `ble_transport_start()` — configure NimBLE SM (KEYBOARD_ONLY, bonding+MITM+SC), MTU pref 512, start host task
2. Load saved active device from settings → attempt `ble_gap_connect()`
3. On CONNECT event: request MTU exchange
4. On MTU exchange: subscribe FromNum CCCD, then call `try_drain()`
5. `try_drain()` — gates on BOTH MTU done AND CCCD subscribed; sends `want_config_id` (incrementing), calls `read_fromradio()`
6. `read_fromradio()` → GAP read → `read_cb()` → `mesh_decode_fromradio()` → `handle_event()` → dispatches to `app_state_set_*()` → chains next read if packet received
7. 600ms poll timer: reads FromRadio periodically; handles read timeouts (1.5s → abandon+retry), CCCD retry (5 attempts), want_config retry (15 attempts), stall watchdog (10s → reconnect, 2 failures → unpair+forget)
8. On CONFIG_COMPLETE: transition to READY, persist device to settings

### Key Invariants
- **Only one read in flight at a time** (s_read_pending guard)
- **FromNum notifications never traverse esp-hosted tunnel** — poll is the sync engine; CCCD subscribe attempted anyway
- **MTU negotiated to 255** (requested 512, negotiated down); first reads gated on MTU exchange completion
- **Each want_config_id must increment** (radio deduplicates by ID)
- **Read timeout = 1.5s** — if read_cb hasn't fired, abandon and re-issue

---

## State Machine

```
BOOT → SCANNING → CONNECTING → ENTER_PIN → PAIRING
                                   ↓
                               SYNCING → READY
                                   ↓         ↓
                            DISCONNECTING  (reconnect)
                                   ↓
                              IDLE / SCANNING
```

States: `BOOT, IDLE, SCANNING, CONNECTING, ENTER_PIN, PAIRING, SYNCING, READY, CONN_ERR, DISCONNECTING`

---

## Protobufs Available (vendored)

In `components/meshtastic_protos/`:
- `mesh.pb.h` — MeshPacket, MyNodeInfo, NodeInfo, FromRadio, ToRadio, Position, User, RouteDiscovery
- `portnums.pb.h` — PortNum enum
- `config.pb.h` — Config, LoRaConfig (region, etc.)
- `channel.pb.h` — Channel, ChannelSettings
- `module_config.pb.h` — ModuleConfig
- `telemetry.pb.h` — Telemetry, DeviceMetrics, EnvironmentMetrics
- `atak.pb.h`, `device_ui.pb.h`, `xmodem.pb.h`

**NOT currently present**: `admin.pb.h` (needed for Phase 4 channel management)

The vendored set is based on Meshtastic ~2.5.x protobufs (nanopb 0.4.9.1).
Exact revision not recorded — must be confirmed before adding admin.pb.

---

## Coupling Points

### BLE Transport → AppState (only allowed direction)
- `app_state_set_conn()` — connection state transitions
- `app_state_set_myinfo()` — my node num + long name
- `app_state_set_diag()` — diagnostic counters
- `app_state_upsert_node()` — per-node data
- `app_state_add_message()` — chat messages
- Settings: `settings_set_active()`, `settings_save_device()`

### UI → BLE (only allowed direction)
- `ble_transport_send_text()` — broadcast chat
- `ble_transport_scan()` — discovery
- `ble_transport_connect()` — connect to addr+PIN
- `ble_transport_submit_pin()` — passkey
- `ble_transport_cancel()` — abort
- `ble_transport_forget()` — remove+unpair

---

## NVS Layout (current)

Namespace: `tab5mesh`
- Key `active_addr` (blob, 6 bytes) — active BLE address
- Key `saved_N` (blob, sizeof(saved_device_t)) for N in 0..7 — saved devices

---

## SPIFFS Partition

Label `storage`, 1 MiB, SPIFFS format. Currently unused by application code.

---

## Thread Model

| Task | Core | Priority | Stack |
|------|------|----------|-------|
| LVGL render (lvgl_port) | CPU1 | 6 | 16KB |
| BLE host (NimBLE) | CPU0 | 5 | ~8KB |
| Keyboard poll | CPU0 | 5 | 4KB |
| app_main | CPU0 | 1 | default |

BLE → UI: `app_state_*()` behind mutex → UI reads snapshot every 500ms
UI → BLE: direct function calls (ble_transport_*) — safe because they use internal queues/locks

---

## Physical Keyboard

- Component: `m5_tab5_keyboard_component` (STM32 A164, I2C 0x6D, I2C_NUM_1)
- Polling mode, 20ms interval (not GPIO ISR — avoids disturbing SDIO)
- `kbd_start()` → `kbd_task` → `ui_kbd_feed()` with LVGL lock
- Routes digits to PIN view, text to chat composer
- OSK is NOT forced open when physical keyboard is in use

---

## Display

- Resolution: 1280×720 landscape
- Rotation: PPA hardware rotation in driver init (LV_DISPLAY_ROTATION_90 + use_ppa)
- Do NOT use runtime LVGL rotation (corrupts render)
- Buffer: full 720×1280 RGB565 in SPIRAM, direct mode
- Backlight: controlled via BSP LEDC channel (not manually)

---

## Known Issues / Tech Debt

1. `ble_transport.cpp` (792 lines) — monolithic; session logic, transport, and protocol dispatch are interleaved
2. `ui_shell.cpp` (1428 lines) — all panels in one file; needs incremental screen extraction
3. `APP_MAX_MSGS = 64` — messages lost after 64; no persistence
4. Settings only persists BLE device list; no transport selection, no UI prefs
5. Channel records from config stream are decoded but discarded (not stored in AppState)
6. `admin.pb` not vendored — channel create/edit not implementable yet
7. No audio subsystem
8. No brightness control API (backlight init in BSP, no runtime adjustment exposed)
