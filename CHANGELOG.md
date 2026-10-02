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

**Channel management** (`main/protocol/`, `main/ui/`)
- Full 8-slot Meshtastic channel editor: name, PSK, uplink/downlink flags
- QR code generation (URL-encoded `MeshChannel` proto) displayed on screen
- Camera-based QR import via M5Tab5 CSI sensor (JPEG decode → proto parse)
- Channel URL encode/decode (`#` fragment, base64url) in `channel_url.cpp`
- `docs/CHANNELS_QR.md`: QR format specification and import flow

**Wi-Fi and time sync** (`main/network/`, `main/app/`)
- Wi-Fi network manager: async scan, save up to 4 SSIDs, auto-reconnect
- SNTP sync after IP acquired; falls back to NTP pool on failure
- Manual RTC time entry in Settings tab (ISO-8601 picker)
- `app_clock.cpp`: unified time source (SNTP-primary, RTC-fallback)
- `docs/CHAT_CLOCK.md`: timestamp rendering rules

**Battery monitor** (`main/board/battery_monitor.cpp`)
- INA226 power-sense IC read via SYS I2C (supports multiple silicon revision IDs)
- Voltage read independently of current sense; exposed in `diag_t`
- Shown in diagnostics strip; tab bar icon reflects charge level

**Camera** (`main/board/tab5_camera.cpp`)
- CSI sensor initialisation with correct LEDC clock source
- Single-frame capture used for QR import; sensor powers down after capture
- Graceful fallback if sensor not detected

**UART text queuing** (`main/protocol/mesh_session.cpp`)
- Outbound text messages queued while `want_config` handshake is in progress
- Flushed and ACK-confirmed once `config_complete` arrives from radio
- Prevents silent text drops on slow UART radio startup

**Launcher 2.8 interoperability** (`main/main.cpp`, `patches/`)
- Clean Tab5 suspend before handing control back to M5Launcher
- ESP32-C6 (`esp_hosted` / BLE) de-inited and SPI/SDIO released on suspend
- Launcher 2.8 keyboard patch (`patches/launcher-2.8.0-tab5-keyboard-rak.patch`)
- Wi-Fi SSID preserved across Launcher retry menu
- OTA app partition sized to fit within Launcher 2.8 limit (≤ 2 MB)
- Recovery image validated; `docs/LAUNCHER.md` and `docs/LAUNCHER_INTEROP.md`

**Headless tests** (`tests/`)
- `chat_ui_test.cpp`: chat scrolling, channel drafts, message deduplication
- `channels_test.cpp`: channel encode/decode round-trip, QR URL parity

**Documentation additions**
- `docs/CHANNELS_QR.md`: QR format, proto encoding, camera import flow
- `docs/CHAT_CLOCK.md`: timestamp source priority and display rules
- `docs/LAUNCHER_INTEROP.md`: M5Launcher 2.8 keyboard and Wi-Fi integration details

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

2. **UART transport**: `MeshSession` and `UartTransport` are wired and functional.
   Runtime switching between BLE and UART without reboot is not yet supported;
   the selector in the Radio tab persists the choice and a restart applies it.

3. **TX/RX polarity on Grove UART**: GPIO53=TX, GPIO54=RX confirmed against Tab5
   schematic. Verify with a loopback test if using a non-standard carrier board.

4. **EXT5V**: Not enabled by default. Set `GROVE_ENABLE_5V=1` only if your
   RAK3172H carrier board declares 5V input.

5. **Camera QR import**: Works on first capture after cold boot; subsequent
   captures may return stale frames if the sensor clock is not re-initialised.
   Workaround: restart the app between QR imports.
