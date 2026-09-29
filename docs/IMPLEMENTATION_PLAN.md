# Implementation Plan

## Status Legend
- [ ] Not started
- [~] In progress
- [x] Complete

---

## Phase 0 — Baseline & Documentation
- [x] Create branch `feature/rak3172-uart-persistence`
- [x] Create UPSTREAM.md (no license → private repo only)
- [x] Create docs/ARCHITECTURE_CURRENT.md
- [x] Create docs/RAK3172_GROVE.md
- [x] Create docs/STORAGE_SCHEMA.md
- [x] Create docs/TEST_MATRIX.md
- [x] Create docs/IMPLEMENTATION_PLAN.md (this file)
- [ ] Verify baseline build (needs ESP-IDF environment)
- [ ] Record baseline binary SHA-256

**Gate**: Documentation complete, baseline commit made.

---

## Phase 1 — Transport/Session Separation

Refactor BLE transport behind `IMeshTransport` interface. Extract `MeshSession`
from `ble_transport.cpp`.

### New files
- `main/transport/mesh_transport.h` — IMeshTransport interface
- `main/transport/ble_transport.h/.cpp` — moved+refactored from main/ble/
- `main/protocol/mesh_session.h/.cpp` — handshake, want_config, state machine

### Changes
- `main/ble/ble_transport.cpp` → extract session logic to `mesh_session.cpp`
- `main/app/app_controller.h/.cpp` — new: owns active transport + session
- `main/app/app_state.h/.cpp` — add channel table (up to 8 channels)
- `main/mesh/mesh_proto.*` — add channel decode (currently discarded)
- Update `main/CMakeLists.txt` and `main/main.cpp`

### Invariants to preserve
- C6L/`C6l_c9e8` discovered by Meshtastic service UUID (not BLE name)
- PIN flow, reconnect, bonding, read-timeout recovery all unchanged
- esp-hosted polling invariants unchanged
- No LVGL calls from transport/session layer
- No direct transport calls from UI (only through AppController)

**Gate**: BLE still works end-to-end; no behavioral regression.

---

## Phase 2 — UART Transport for RAK3172H

### New files
- `main/transport/uart_transport.h/.cpp`
- `main/transport/serial_framer.h/.cpp`
- `main/board/tab5_uart.h/.cpp` — GPIO53/54 UART driver init

### Changes
- `main/app/app_controller.*` — transport selection, UART connect flow
- `main/app/settings.*` — persist transport selection
- `main/ui/screens/radio_screen.*` — add BLE/UART selector, UART status

### SerialFramer requirements
- Non-blocking, byte-at-a-time or chunked input
- Handles: split headers, split payloads, multi-frame reads, garbage before header
- Handles: invalid magic, oversized frames (>512B), incomplete frames, timeouts
- Resync without app restart

### UART init sequence
1. Enable EXT5V only if carrier supports 5V (gated by build flag / NVS flag)
2. Init UART2 on GPIO53(TX)/GPIO54(RX), 115200 8N1, no flow control
3. Allocate 1024-byte RX ring buffer
4. Dedicated FreeRTOS task (UART_TASK)
5. On init: optionally absorb `FromRadio.rebooted`
6. Send `ToRadio.want_config_id`
7. Process frames until `config_complete_id`
8. Enter READY state; continue receiving spontaneous frames

**Gate** (needs hardware):
- RAK appears connected; my_info received; nodes appear
- Region ANZ preserved; messages sent/received
- Reconnects after cable removal; BLE unaffected

---

## Phase 3 — Persistence & History

### New files
- `main/storage/app_storage.h/.cpp` — SPIFFS mount/unmount, availability flag
- `main/storage/message_store.h/.cpp` — append, load page, compact, dedup
- `main/storage/settings_store.h/.cpp` — read/write settings.bin
- `main/storage/storage_migrations.h/.cpp` — first-boot import from NVS

### Changes
- `main/app/app_state.*` — add channel index, message pagination support
- `main/app/settings.*` — delegate to settings_store, add new keys
- `main/ui/screens/chat_screen.*` — load history on open, paginate
- Boot: if SPIFFS unavailable → operate in RAM-only mode, show warning

**Gate**: History and settings survive reboot; app starts without SPIFFS.

---

## Phase 4 — Channel Management

### New files
- `main/protocol/channel_service.h/.cpp`
- `main/protocol/admin_client.h/.cpp`
- `main/ui/screens/channels_screen.h/.cpp`

### Changes
- `components/meshtastic_protos/` — add `admin.pb.c/.h` (pinned revision)
- `main/app/app_state.*` — store channel table (8 slots)
- `main/mesh/mesh_proto.*` — decode and store `FromRadio.channel`
- `main/protocol/mesh_session.*` — stop discarding channel records

### Pinned protobuf revision
Must match firmware 2.7.26 protobufs. Tag: `v2.7.26` in meshtastic/protobufs.
Regenerate with nanopb 0.4.9.1. Document command in admin_client.h.

### PSK handling
- 0 bytes = no encryption
- 1 byte = shortcut (0x01 = default AES, etc.)
- 16 bytes = AES-128
- 32 bytes = AES-256 (default for new private channels)
- Generate private PSK with `esp_fill_random()` (hardware RNG)
- Never log PSK bytes
- Show only fingerprint (SHA256 first 4 bytes, hex) in UI

**Gate**: Channel list visible; new channel created from Tab5; PSK set; chat uses correct channel index.

---

## Phase 5 — Display & Audio Settings

### New files
- `main/board/tab5_audio.h/.cpp` — ES8388 audio via BSP
- `main/board/tab5_power.h/.cpp` — EXT5V, SPK_EN via expander
- `main/ui/screens/settings_screen.h/.cpp`

### Changes
- `main/board/lcd_tools.*` — expose `lcd_set_brightness(uint8_t percent)` using LEDC
- `main/app/settings.*` — brightness, notif_en, notif_vol, notif_pat

### Audio constraints
- Non-blocking beep via I2S + DMA or timer-driven synthesis
- Triggered only for: new message from network (not self-sent, not history load)
- ES8388 init follows BSP/UserDemo reference exactly
- SPK_EN via PI4IOE5V6408 before playback

**Gate**: Brightness slider works; beep patterns play; settings persist.

---

## Phase 6 — M5Launcher Compatibility

### New files / changes
- `docs/LAUNCHER.md` — OTA instructions, Associate to Bin, backup/restore

### Build changes
- Verify `partitions.csv` produces correct layout for OTA
- Output: `tab5-meshtastic-rak3172-ota.bin` (app binary only, not full-flash)
- `SHA256SUMS.txt`

**Gate**: Bin installs via Launcher OTA; settings/history survive firmware swap.

---

## Phase 7 — QA & Release

- [ ] Serial framer unit tests (native or qemu)
- [ ] Storage unit tests
- [ ] Channel logic unit tests
- [ ] Hardware test: BLE (C6L, UUID discovery, reconnect)
- [ ] Hardware test: UART (RAK3172H, ANZ preserved, reconnect)
- [ ] Hardware test: persistence (cold boot, Launcher swap)
- [ ] Hardware test: keyboard (chat, PIN, channels, settings)
- [ ] Hardware test: audio (each beep pattern)
- [ ] Final build: no errors, binary named correctly
- [ ] SHA256SUMS.txt generated
- [ ] CHANGELOG.md written
- [ ] Docs reviewed

---

## Commit Convention

Each phase ends with at least one commit:
```
feat(phase1): extract IMeshTransport and MeshSession from BLE transport
feat(phase2): add UART transport for RAK3172H via Grove
feat(phase3): add persistent message history and settings
feat(phase4): add channel management with admin.pb
feat(phase5): add brightness and audio notification settings
feat(phase6): M5Launcher OTA compatibility and backup docs
```
