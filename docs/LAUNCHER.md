# M5Launcher — OTA, Backup and Restore

> Device check (2026-10-01): this Tab5 currently has a Launcher-managed
> `tab5me` app partition of **1600 KiB** at `0x1a0000`. The `partitions.csv`
> below describes a standalone full-flash installation; it is not the table
> used by the running Launcher. OTA app binaries for this installation must be
> at most 1,638,400 bytes. The recovery build uses size optimization and a
> 16 KiB main-task stack; do not select the older `d46a978` OTA image, which
> has a 4 KiB main-task stack and crashes during display startup.

## Overview

The M5Launcher (https://github.com/bmorcelli/Launcher) allows installing and
switching between applications on the M5Stack Tab5 without a USB cable, using
OTA updates loaded from SD card or HTTP.

This document describes how to install, back up, and restore
Tab5-Meshtastic-RAK3172 using the Launcher.

---

## Building the OTA Binary

The application binary for OTA is the plain app binary — **not** a full-flash
image. It does not include the partition table or bootloader.

```bash
# In the project directory with ESP-IDF sourced:
idf.py build

# The OTA binary is at:
build/tab5_meshtastic_v2.bin

# Rename to the expected name:
cp build/tab5_meshtastic_v2.bin tab5-meshtastic-rak3172-ota.bin

# Generate checksum:
sha256sum tab5-meshtastic-ota.bin > SHA256SUMS.txt
```

**IMPORTANT**: The OTA binary does NOT modify the partition table. The partition
layout already present in the device (from a previous idf.py flash) is used.
This means the `storage` SPIFFS partition must already exist on the device before
installing via OTA. If the device was flashed from scratch with
`idf.py flash`, the correct partition table is already in place.

---

## Prerequisites

1. M5Launcher installed on the Tab5 (flashed via USB using a full-flash image).
2. An SD card formatted as FAT32, inserted in the Tab5's SD card slot.
3. The OTA binary `tab5-meshtastic-rak3172-ota.bin` copied to the SD card.

---

## Installing via Launcher OTA

1. Copy `tab5-meshtastic-rak3172-ota.bin` to the root of the SD card.
2. Boot the Tab5 with the SD card inserted.
3. Open M5Launcher.
4. Navigate to **App Manager** → **From SD**.
5. Select `tab5-meshtastic-rak3172-ota.bin`.
6. Confirm the flash.
7. The Launcher will write the binary to the `factory` partition and reboot.
8. On reboot, the Tab5 loads Tab5-Meshtastic-RAK3172.

---

## Associate to Bin (persistent launch)

To make Launcher always boot Tab5-Meshtastic by default:

1. In Launcher, go to **App Manager** → **Installed Apps**.
2. Select `tab5-meshtastic-rak3172-ota.bin` → **Associate to Bin**.
3. Launcher will boot into this app automatically on next power-on.

Reference: https://github.com/bmorcelli/Launcher/wiki/Functionalities-explained

---

## Backing Up App Data (requires SD card)

The persistent app data is stored in the `storage` SPIFFS partition.
Backup consists of reading that partition to a file on the SD card.

**Via Launcher PMan (Partition Manager)**:
1. Boot into M5Launcher.
2. Navigate to **PMan** → **Partitions**.
3. Locate the `storage` partition.
4. Select **Backup to SD** (saves `storage.bin` to SD card).
5. Copy `storage.bin` from SD card to a safe location.

If PMan is not available in your Launcher version, backup can be done via the
`esptool.py read_flash` command over USB (see "Manual Backup" below).

**Manual Backup (USB)**:
```bash
# Find the offset and size of the storage partition from partitions.csv:
# storage = SPIFFS, offset auto (after 6MB factory app), size 1MB

# Read the storage partition (offset must match partitions.csv):
esptool.py -p /dev/ttyACM0 read_flash 0x610000 0x100000 storage_backup.bin
```

---

## Restoring App Data

1. Copy `storage.bin` (or `storage_backup.bin`) to the SD card.
2. Boot into M5Launcher → **PMan** → **Partitions** → `storage` → **Restore from SD**.
3. Confirm restore.
4. Reboot into Tab5-Meshtastic.

**Manual Restore (USB)**:
```bash
esptool.py -p /dev/ttyACM0 write_flash 0x610000 storage_backup.bin
```

---

## What is Backed Up

The `storage` SPIFFS partition contains:
- `settings.bin` — transport selection, brightness, notifications, selected channel
- `messages.bin` — full message history
- `channels.bin` — channel table (including PSKs — handle with care)

**PSK warning**: The `channels.bin` file contains channel PSKs. The SPIFFS
partition is NOT encrypted. The backup file contains channel keys in plaintext.
Keep backup files in a secure location.

NimBLE bonds (device pairing data) are stored in the NVS partition, NOT in
`storage`. They are NOT included in this backup. After a device replacement or
NVS erase, you will need to re-pair BLE devices.

---

## Switching Firmware and Returning

1. Boot into Launcher.
2. Switch to another app (e.g. M5Synth) — Tab5-Meshtastic data remains in SPIFFS.
3. Return by launching `tab5-meshtastic-rak3172-ota.bin` again from App Manager.
4. All settings and history are intact (the `storage` partition was not touched).

If another app formats or uses the `storage` partition with an incompatible
layout, Tab5-Meshtastic will detect the mismatch and operate in RAM-only mode.
No crash or boot loop will occur — the user will see "Storage unavailable" in
the Radio tab status.

---

## Partition Layout Reference

From `partitions.csv`:

| Name     | Type | Offset   | Size  | Contents |
|----------|------|----------|-------|----------|
| nvs      | data | 0x9000   | 24KB  | NimBLE bonds, app NVS |
| phy_init | data | 0xF000   | 4KB   | RF calibration |
| factory  | app  | 0x10000  | 6MB   | Application binary |
| storage  | data | 0x610000 | 1MB   | SPIFFS (messages, settings) |

Note: The offset of `storage` is derived from the `factory` partition end:
`0x10000 + 6*1024*1024 = 0x610000`.

---

## Full Flash Image (Initial Setup Only)

A full flash image is only needed for a blank device or after NVS/partition
corruption. It is NOT used for OTA updates.

**DESTRUCTIVE** — erases all bonds, settings, and history:
```bash
idf.py -p /dev/ttyACM0 flash
```

Or using the merged binary from idf.py:
```bash
esptool.py -p /dev/ttyACM0 write_flash 0x0 build/merged-binary.bin
```

Do not use a full flash image for routine OTA — it will erase all user data.
