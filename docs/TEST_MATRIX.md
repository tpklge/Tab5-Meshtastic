# Test Matrix

## Status Legend
- [ ] Not run
- [P] Pass
- [F] Fail
- [S] Skipped (no hardware / N/A)

---

## Automated Tests (host/native)

### Serial Framer
| # | Test | Status | Notes |
|---|------|--------|-------|
| SF-01 | Complete valid frame | [ ] | |
| SF-02 | Frame input byte-by-byte | [ ] | |
| SF-03 | Header split across two reads | [ ] | |
| SF-04 | Payload split across multiple reads | [ ] | |
| SF-05 | Two frames in same read buffer | [ ] | |
| SF-06 | Garbage bytes before valid header | [ ] | |
| SF-07 | Invalid magic bytes | [ ] | |
| SF-08 | Length > 512 (max) → rejected | [ ] | |
| SF-09 | Incomplete frame (timeout) | [ ] | |
| SF-10 | Resync after invalid frame | [ ] | |
| SF-11 | Length = 0 | [ ] | |
| SF-12 | Exact max-length frame (512 bytes) | [ ] | |

### Storage
| # | Test | Status | Notes |
|---|------|--------|-------|
| ST-01 | Write and read back valid settings record | [ ] | |
| ST-02 | Corrupt CRC in settings.bin → use defaults | [ ] | |
| ST-03 | Append 10 messages, load all | [ ] | |
| ST-04 | Truncated last record → skip, load rest | [ ] | |
| ST-05 | Duplicate (from_node, packet_id, channel) → deduplicated | [ ] | |
| ST-06 | Schema version migration (v0 NVS → v1 file) | [ ] | |
| ST-07 | Retention: exceed 10000 records → compact | [ ] | |
| ST-08 | Retention: exceed 800KB → compact | [ ] | |
| ST-09 | Paginated load: 250 records, load page 2 | [ ] | |
| ST-10 | SPIFFS unavailable → app starts, RAM-only mode | [ ] | |
| ST-11 | Atomic write: crash during write → previous valid state recoverable | [ ] | |

### Channels
| # | Test | Status | Notes |
|---|------|--------|-------|
| CH-01 | Decode channel records from config stream | [ ] | |
| CH-02 | Send message with correct channel index | [ ] | |
| CH-03 | Only one PRIMARY channel after set_channel | [ ] | |
| CH-04 | Create SECONDARY channel | [ ] | |
| CH-05 | Disable/remove a channel | [ ] | |
| CH-06 | PSK 0 bytes = no encryption | [ ] | |
| CH-07 | PSK 1 byte shortcut accepted | [ ] | |
| CH-08 | PSK 16 bytes (AES-128) accepted | [ ] | |
| CH-09 | PSK 32 bytes (AES-256) accepted | [ ] | |
| CH-10 | PSK 2 bytes → rejected (invalid length) | [ ] | |
| CH-11 | PSK never appears in logs | [ ] | |
| CH-12 | Private channel PSK generated with hardware RNG | [ ] | |
| CH-13 | Channel name > max bytes UTF-8 → rejected | [ ] | |

---

## Hardware Tests — BLE

| # | Test | Status | Notes |
|---|------|--------|-------|
| BLE-01 | Discover C6L by Meshtastic service UUID (any BLE name) | [ ] | |
| BLE-02 | Discover `C6l_c9e8` (non-standard name) | [ ] | |
| BLE-03 | Pair with PIN 123456 | [ ] | |
| BLE-04 | Reconnect after Tab5 restart (bonded) | [ ] | |
| BLE-05 | Node list populated after sync | [ ] | |
| BLE-06 | Telemetry (battery, metrics) displayed | [ ] | |
| BLE-07 | Send text message, appears in chat | [ ] | |
| BLE-08 | Receive text message from another node | [ ] | |
| BLE-09 | Disconnect + reconnect (BLE radio power cycle) | [ ] | |
| BLE-10 | Stall watchdog fires after 10s no data → reconnects | [ ] | |
| BLE-11 | After 2 stall-reconnect failures → unpair + forget | [ ] | |

---

## Hardware Tests — UART (RAK3172H)

| # | Test | Status | Notes |
|---|------|--------|-------|
| UART-01 | Select UART transport in Radio settings | [ ] | |
| UART-02 | RAK3172H appears connected (my_info received) | [ ] | |
| UART-03 | Node list populated after sync | [ ] | |
| UART-04 | Send text message | [ ] | |
| UART-05 | Receive text message | [ ] | |
| UART-06 | Region ANZ preserved (not changed) | [ ] | |
| UART-07 | Reconnect after Grove cable removal and reinsertion | [ ] | |
| UART-08 | Reconnect after Tab5 reboot (UART not re-flashed) | [ ] | |
| UART-09 | Reconnect after RAK3172H power cycle | [ ] | |
| UART-10 | Frame counter increments in Radio status view | [ ] | |
| UART-11 | BLE still functional when UART selected (can switch back) | [ ] | |

---

## Hardware Tests — Persistence

| # | Test | Status | Notes |
|---|------|--------|-------|
| PERS-01 | Settings survive Tab5 reboot | [ ] | |
| PERS-02 | Message history survives Tab5 reboot | [ ] | |
| PERS-03 | Settings survive cold boot (battery, not USB reset) | [ ] | |
| PERS-04 | History survives cold boot | [ ] | |
| PERS-05 | Switch to different firmware via Launcher → return → history intact | [ ] | |
| PERS-06 | Backup via Launcher SD → delete data → restore → history intact | [ ] | |
| PERS-07 | NimBLE bonds NOT erased by migration | [ ] | |

---

## Hardware Tests — UI

| # | Test | Status | Notes |
|---|------|--------|-------|
| UI-01 | Touchscreen: all buttons/tabs respond | [ ] | |
| UI-02 | LVGL OSK: type in chat via touch | [ ] | |
| UI-03 | Physical keyboard: type text in chat | [ ] | |
| UI-04 | Physical keyboard: Enter sends message | [ ] | |
| UI-05 | Physical keyboard: Backspace works | [ ] | |
| UI-06 | Physical keyboard: special characters | [ ] | |
| UI-07 | Physical keyboard typing does NOT open OSK automatically | [ ] | |
| UI-08 | Type channel name with physical keyboard | [ ] | |
| UI-09 | Type PSK in Base64 with physical keyboard | [ ] | |
| UI-10 | Brightness slider: immediate effect, persists after reboot | [ ] | |
| UI-11 | Volume slider: immediate effect, persists | [ ] | |
| UI-12 | Beep pattern: each pattern plays correctly | [ ] | |
| UI-13 | Test beep button: plays selected pattern | [ ] | |
| UI-14 | No LVGL rendering corruption after 30min use | [ ] | |
| UI-15 | No UI freeze/watchdog after 30min use | [ ] | |

---

## Build Verification

| # | Check | Status | Notes |
|---|-------|--------|-------|
| BLD-01 | Clean build, zero errors | [ ] | |
| BLD-02 | Zero new warnings vs baseline | [ ] | |
| BLD-03 | Binary named `tab5-meshtastic-rak3172-ota.bin` | [ ] | |
| BLD-04 | SHA256SUMS.txt generated | [ ] | |
| BLD-05 | Binary installs via Launcher OTA | [ ] | |
| BLD-06 | Cold boot after OTA: app starts correctly | [ ] | |
