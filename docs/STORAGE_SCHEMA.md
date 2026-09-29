# Storage Schema

## Partition Layout

```
# partitions.csv
nvs,      data, nvs,     0x9000,  0x6000,    — NVS (NimBLE bonds + app settings)
phy_init, data, phy,     0xf000,  0x1000,    — RF calibration
factory,  app,  factory, 0x10000, 6M,        — Application binary
storage,  data, spiffs,  ,        1M,        — SPIFFS persistent data
```

The `storage` SPIFFS partition (label `storage`) holds all persistent data
added by this fork. The application must tolerate the partition being absent
(first flash without a previous `idf.py flash`, or different firmware that
omits this partition).

---

## NVS Layout

Namespace: `tab5mesh`

### Existing (preserved)
| Key | Type | Description |
|-----|------|-------------|
| `active_addr` | blob (6B) | Active BLE device address |
| `saved_N` | blob (sizeof saved_device_t) | Saved BLE devices (N=0..7) |

NimBLE bond store uses its own internal NVS namespace — never touch it.

### New keys added by this fork
| Key | Type | Description |
|-----|------|-------------|
| `transport` | uint8 | Selected transport: 0=BLE, 1=UART |
| `brightness` | uint8 | Backlight 5–100 (percent) |
| `notif_en` | uint8 | Notifications enabled: 0/1 |
| `notif_vol` | uint8 | Volume 0–100 |
| `notif_pat` | uint8 | Beep pattern: 0=silent,1=short,2=double,3=triple |
| `sel_channel` | uint8 | Selected channel index for chat (0=primary) |

---

## SPIFFS File Layout

All files under mount point `/spiffs` (configured in app_storage.cpp).

```
/spiffs/
  settings.bin     — App settings record (versioned)
  messages.bin     — Message history log (versioned, append-only segments)
  channels.bin     — Channel table snapshot (versioned)
```

---

## File Format: settings.bin

Simple fixed-size record, rewritten on change.

```c
// Version 1
struct settings_file_v1 {
    uint32_t magic;       // 0x54354D01 ('T5M' + version 1)
    uint8_t  version;     // schema version = 1
    uint8_t  transport;   // 0=BLE, 1=UART
    uint8_t  brightness;  // 5–100
    uint8_t  notif_en;    // 0/1
    uint8_t  notif_vol;   // 0–100
    uint8_t  notif_pat;   // 0–3
    uint8_t  sel_channel; // 0–7
    uint8_t  _pad[1];
    uint32_t crc32;       // CRC32 of bytes [0..11]
};
// Total: 16 bytes
```

On read: verify magic, version, crc32. If invalid, use defaults (do not crash).
On write: fill struct, compute crc32, write atomically via temp-file rename.

---

## File Format: messages.bin

Append-only log. Each segment is a fixed header + variable payload.

```
File layout:
  [file_header_t]               — 16 bytes
  [msg_record_t] ...            — zero or more records
```

```c
// File header (written once at creation)
struct file_header_t {
    uint32_t magic;    // 0x54354D02 ('T5M' + version 2)
    uint8_t  version;  // 1
    uint8_t  _pad[3];
    uint32_t count;    // number of valid records (updated on compaction)
    uint32_t crc32;    // CRC32 of bytes [0..11]
};

// Message record (appended per message)
struct msg_record_t {
    uint32_t magic;       // 0x4D534754 ('MSGT')
    uint32_t seq;         // local sequence number (monotonically increasing)
    uint8_t  version;     // 1
    uint8_t  direction;   // 0=received, 1=sent
    uint8_t  channel_idx; // 0–7
    uint8_t  is_broadcast;// 0=direct, 1=broadcast
    uint32_t from_node;   // sender node num
    uint32_t to_node;     // recipient (0=broadcast)
    uint32_t packet_id;   // Meshtastic packet ID (for dedup)
    int64_t  timestamp_us;// ESP monotonic time (esp_timer_get_time()) or 0
    int64_t  abs_time_s;  // Unix timestamp if valid (RTC/GPS), else 0
    int16_t  snr;         // SNR * 4 (fixed point, INT16_MIN if unknown)
    int16_t  rssi;        // RSSI dBm (INT16_MIN if unknown)
    uint16_t text_len;    // length of text in bytes
    uint8_t  _pad[2];
    uint32_t crc32;       // CRC32 of all bytes before this field
    char     text[];      // text_len bytes, NO null terminator in file
};
// Fixed part: 52 bytes; total = 52 + text_len (+ any alignment padding)
```

### Deduplication Key
`(from_node, packet_id, channel_idx)` — ignore duplicate records on load.

### Retention Policy
- Maximum size: 800 KB (leaves 200 KB headroom in 1 MB partition)
- Maximum records: 10,000
- On limit: compact oldest 20% of records into a temp file, replace

### Crash Safety
- Appends are a single `fwrite` + `fflush`
- A truncated last record is detected by CRC mismatch — skip and log warning
- File header `count` is only updated during compaction (not per-append)
- Loading counts valid records by scanning from the start

### Pagination
Load at most 200 most-recent records into RAM cache on demand.
UI requests pages via `message_store_load_page(channel, page, records, count)`.

---

## File Format: channels.bin

Snapshot of the full channel table (written after any channel change).

```c
struct channels_file_t {
    uint32_t magic;    // 0x54354D03 ('T5M' + version 3)
    uint8_t  version;  // 1
    uint8_t  count;    // number of channel entries
    uint8_t  _pad[2];
    uint32_t crc32;    // CRC32 of bytes [0..7]
    channel_entry_t channels[8];
};

struct channel_entry_t {
    uint8_t  index;       // 0–7
    uint8_t  role;        // 0=disabled, 1=primary, 2=secondary
    uint8_t  name[12];    // UTF-8, null-terminated
    uint8_t  psk[32];     // AES-256 key or zero-filled
    uint8_t  psk_len;     // 0=none, 1=shortcut byte, 16=AES-128, 32=AES-256
    uint8_t  uplink_en;   // 0/1
    uint8_t  downlink_en; // 0/1
    uint8_t  _pad;
    uint32_t crc32;       // CRC32 of this entry bytes [0..47]
};
// channel_entry_t: 52 bytes
// channels_file_t header: 12 bytes + 8 * 52 = 428 bytes total
```

PSKs are NOT logged. Channel file is not backed up to SD card in plaintext
unless the user explicitly chooses to export.

---

## Migration

On first boot after update from pre-fork firmware:
1. Attempt to open `settings.bin` — if missing, create with defaults
2. Read NVS keys `transport`, `brightness`, etc. — if missing, use defaults
3. NimBLE bonds in NVS are untouched
4. `messages.bin` — if missing, create empty with file_header_t
5. `channels.bin` — if missing, create empty (channels will be populated from next sync)

No destructive migration: never erase NVS namespace, never reformat SPIFFS.
