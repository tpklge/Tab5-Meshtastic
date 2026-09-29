# RAK3172H via Grove — Hardware Reference

## Grove/HY2.0-4P Pinout (Tab5)

| Pin | Signal | GPIO |
|-----|--------|------|
| 1   | GND    | —    |
| 2   | 5V     | —    |
| 3   | SDA/IO | GPIO53 |
| 4   | SCL/IO | GPIO54 |

GPIO53 and GPIO54 are routed through the ESP32-P4 GPIO matrix and can be
configured for UART TX/RX.

**IMPORTANT — TX/RX assignment must be confirmed against the Tab5 schematic
before first flash.** The Grove connector's pin 3 and pin 4 labels (SDA/SCL)
are Grove defaults; which one becomes UART TX and which becomes RX is
determined by the actual PCB trace and carrier board wiring of the RAK3172H.

Do NOT swap TX/RX by trial — two outputs driving each other (TX↔TX) can cause
bus contention. Confirm by measuring or consulting the schematic.

Assumed assignment (to be verified):
- GPIO53 → UART TX (Tab5 transmits → RAK3172H U1RX/PB7)
- GPIO54 → UART RX (Tab5 receives ← RAK3172H U1TX/PB6)

## RAK3172H UART Parameters

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity    | None |
| Stop bits | 1 |
| Flow ctrl | None |
| Logic level | 3.3V |

## RAK3172H Meshtastic Serial Module Configuration

- Mode: `PROTO` (raw protobuf, not AT commands)
- Region: ANZ (must not be changed by the Tab5 application)

## Serial Framing (Meshtastic Client API)

Every message is a framed protobuf:

```
Offset  Size  Value/Description
  0     1     0x94  (magic byte 1)
  1     1     0xC3  (magic byte 2)
  2     2     uint16 big-endian — payload length in bytes
  4     N     raw protobuf bytes (ToRadio or FromRadio)
```

- Maximum payload: 512 bytes (enforce before allocation)
- Direction Tab5→RAK: ToRadio protobuf
- Direction RAK→Tab5: FromRadio protobuf

## Power Notes

**CRITICAL**: The 5V rail on Grove must NOT be connected to a bare RAK3172H
module's power or UART pins. The RAK3172H operates at 3.3V logic.

Only connect 5V to the Grove power pin if your RAK3172H carrier board has a
5V→3.3V regulator and declares 5V input on its power rail. Verify the carrier
board datasheet before enabling EXT5V via the PI4IOE5V6408 expander.

The UART signal lines (GPIO53/GPIO54) are 3.3V logic when configured — safe for
direct connection to RAK3172H UART pins without level shifting.

## Enabling EXT5V (PI4IOE5V6408)

The Tab5's I/O expander (PI4IOE5V6408 at I2C_NUM_0) controls `EXT5V_EN`.
Reference the m5_tab5_component for the correct bit/register to set.

In the application, EXT5V should be enabled only after confirming the carrier
board supports 5V input. Add a build-time or NVS flag to control this.

## RAK3172H GPIO Connections

| RAK Pin | Function | Connects to |
|---------|----------|-------------|
| PB7 (U1RX) | UART receive | Tab5 GPIO53 (TX) |
| PB6 (U1TX) | UART transmit | Tab5 GPIO54 (RX) |
| GND | Ground | Grove GND |
| 3V3 or VCC | Power | Carrier 3.3V supply |

## Connection Procedure

1. Power off both devices
2. Connect GND → GND
3. Connect TX (GPIO53) → RAK PB7 (U1RX) — confirm polarity first
4. Connect RX (GPIO54) → RAK PB6 (U1TX) — confirm polarity first
5. Power carrier board via dedicated supply or regulated Grove 5V
6. Power on Tab5
7. Select "RAK3172H (Grove UART)" in Radio settings

## Reconnection Behavior

After Grove cable removal and reinsertion:
- The UART task detects frame timeout / UART error
- On next connection attempt, reinitializes the UART driver
- Sends `want_config_id` and waits for `config_complete_id`
- The application must NOT restart or reconfigure the RAK at this point
- The region `ANZ` must be preserved as received from the radio
