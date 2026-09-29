# Changelog

## [Unreleased] — feature/rak3172-uart-persistence

### Added

**Transport abstraction layer** (`main/transport/`)
- `IMeshTransport` interface: start/stop/send/poll/stats/name
- `SerialFramer`: non-blocking 0x94C3 Meshtastic serial frame parser
  - Handles split headers, split payloads, multi-frame reads, garbage sync
  - Timeout detection, resync without restart, stats counter
- `UartTransport`: UART2 driver on Grove GPIO53(TX)/GPIO54(RX) at 115200 8N1
  - Dedicated FreeRTOS task (non-blocking)
  - TX queue (8 items), RX ring buffer (2KB)

**Protocol session layer** (`main/protocol/`)
- `MeshSession`: reusable Meshtastic protocol handling above any transport
  - want_config_id handshake, config_complete detection
  - Dispatches decoded FromRadio events into AppState

**Persistent storage** (`main/storage/`)
- `AppStorage`: SPIFFS mount with graceful fallback (no boot loop if absent)
- `MessageStore`: append-only versioned binary log
  - CRC32 per record, deduplication by (from_node, packet_id, channel)
  - Truncation recovery, paginated load (200 records/page)
  - Retention limit: 10,000 records / 800KB
- `SettingsStore`: atomic settings file (CRC32, temp-file rename)
  - Persists: transport selection, brightness, notifications (en/vol/pattern), channel

**Display**
- `lcd_set_brightness()`: runtime backlight control via LEDC_CHANNEL_1
  - Uses the LEDC channel already initialized by BSP — no re-init
  - Range: 5–100%, applied immediately and persisted
  - Restored from settings on every cold boot

**Audio** (`main/board/tab5_audio.h/.cpp`)
- ES8388 codec init via system I2C + I2S
- Non-blocking beep patterns via dedicated FreeRTOS task
- Patterns: silent / short / double / triple
- Volume: 0–100%, applied to ES8388 hardware registers
- Graceful fallback if I2C or I2S initialization fails
- New messages from network trigger configurable beep pattern

**Settings UI** (`main/ui/screens/settings_screen.h/.cpp`)
- New 4th nav tab "SET" added to the rail
- Brightness slider (5–100%) with live preview and "Reset to default"
- Notification enable toggle
- Volume slider (0–100%)
- Pattern dropdown (silent/short/double/triple)
- "Test sound" button

**Transport selector in Radio tab**
- BLE and RAK3172H (Grove UART) selector buttons at top of Radio panel
- Selection persisted to settings_store

**Boot sequence updates** (`main/main.cpp`)
- Initialize SPIFFS, settings_store, message_store after NVS
- Restore saved brightness before first LVGL frame
- Initialize audio after keyboard, apply saved volume

**Documentation** (`docs/`, `UPSTREAM.md`, `CHANGELOG.md`)
- `UPSTREAM.md`: license status (no license found → private repo only)
- `docs/ARCHITECTURE_CURRENT.md`: BLE flow, state machine, coupling points
- `docs/IMPLEMENTATION_PLAN.md`: phased checklist
- `docs/RAK3172_GROVE.md`: Grove pinout, wiring, 0x94C3 framing protocol
- `docs/STORAGE_SCHEMA.md`: versioned binary file formats with CRC32
- `docs/TEST_MATRIX.md`: automated + hardware test cases
- `docs/LAUNCHER.md`: OTA installation, backup/restore, partition layout

### Preserved (no regression)
- BLE transport: C6L discovery by service UUID, PIN flow, reconnect, bonding
- Physical keyboard: chat typing, PIN entry, no forced OSK
- Display: 1280×720 PPA hardware rotation (unchanged)
- Node list: sort, signal bars, detail view
- Chat bubbles: incremental append, scroll to newest
- Diagnostics strip: long-press on status bar

### Known Limitations

1. **Audio**: The `m5_tab5_component` BSP does not expose the SYS I2C bus handle
   or a `spk_enable()` method. The audio driver attempts to create a new I2C master
   bus on I2C_NUM_0 (same pins). If the BSP takes an exclusive lock, audio init
   fails gracefully (non-fatal). Fix: add `get_sys_i2c_bus()` and `spk_enable()`
   to the BSP component.

2. **UART transport**: `MeshSession` and `UartTransport` are implemented but not
   yet wired to the AppController (transport switch from BLE to UART). The UI
   selector persists the choice but switching requires a restart. Full runtime
   switching planned for next phase.

3. **Channel management**: `admin.pb.h` not yet added (requires pinning protobuf
   revision 2.7.26). Channel create/edit not yet implementable. Phase 4 pending.

4. **TX/RX polarity on Grove UART**: GPIO53=TX, GPIO54=RX assumed but must be
   confirmed against Tab5 schematic before first hardware test.

5. **EXT5V**: Not enabled by default. Set `GROVE_ENABLE_5V=1` only if your
   RAK3172H carrier board declares 5V input.
