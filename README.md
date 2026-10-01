# Tab5-Meshtastic v2

A [Meshtastic](https://meshtastic.org) client for the **M5Stack Tab5**
(ESP32-P4 + ESP32-C6), with **BLE or Grove UART** connections to an external
LoRa radio. Browse nodes, chat on separate channels, create and share channel
configurations, and manage Wi-Fi, time, and device settings from the touchscreen.

This fork builds on [hardparking/Tab5-Meshtastic](https://github.com/hardparking/Tab5-Meshtastic).
The Tab5 provides the interface; **LoRa communication requires an external radio**.
Its onboard ESP32-C6 supplies Wi-Fi and Bluetooth through esp-hosted (SDIO).
BLE remains available alongside the added RAK3172H UART transport; one radio
transport is active at a time.

[`PRD.md`](PRD.md) records the original design. The features and guides below
describe the additions implemented in this fork.

![Nodes view](docs/nodes-view.png)

> The **Nodes** view — live mesh with signal, hops, and last-heard. (Design
> reference; the on-device render tracks it closely, if not pixel-for-pixel.)

## Features

- **BLE and Grove UART radios** — BLE discovery, PIN pairing, saved devices and
  automatic reconnection; RAK3172H serial connection at 115200 baud through Grove.
  Select the transport in **RADIO** and restart the app to apply it. New settings
  default to UART; BLE support has not been removed.
- **Nodes and telemetry** — live node list with signal, hops, last-heard, identity,
  position and device metrics when provided by the radio.
- **Chat by channel** — choose the channel directly in the chat header to filter
  messages and send to that channel. Includes scrolling history, a jump to the
  latest messages, session drafts per channel, and physical/on-screen keyboards.
  With the RAK on UART, outgoing texts wait for radio startup, are paced to
  respect the two-second Meshtastic limit, and appear in history after the
  radio confirms queue acceptance. A refused text is retried; an unconfirmed
  text is offered back to an empty composer for review.
  The app retains up to 64 messages in RAM and the latest 30 across restarts,
  shared across all channels.
- **Channel management** — read the radio's eight slots, create secondary channels,
  edit names and Base64 keys, generate AES-256 keys, configure MQTT uplink/downlink
  flags, and disable secondary channels. Writes are followed by a readback check.
- **QR sharing and camera import** — display a QR for one channel or a channel set;
  scan with the Tab5 camera or paste a Meshtastic channel URL. Review before adding
  or replacing channels. Optional LoRa settings from the URL preserve the radio's
  region and transmit power. Shared QR codes contain the channel keys.
- **Wi-Fi settings** — scan networks, enter an SSID/password, save and reconnect,
  view the assigned IP, disconnect, or forget a network from **SET**. Scanning runs
  asynchronously outside the UI task.
- **Internet and manual time** — synchronize through SNTP over Wi-Fi or set the
  date/time manually. Time is stored in the Tab5 RTC, with a saved UTC offset;
  GPS is not required. Daylight-saving changes are manual.
- **Tab5 battery status** — measured voltage and an approximate percentage in the
  top bar. The `~` percentage is estimated from voltage, not a charge counter;
  unavailable readings show `--`. Node battery telemetry is separate.
- **Display and sound** — saved brightness and audio notification settings,
  including volume and notification pattern.
- **Diagnostics** — connection status, radio identity and a long-press diagnostics
  strip; BLE uses polling and read-timeout recovery.
- **Launcher deployment** — build an application binary for installation through
  the launcher's OTA workflow, without opening a serial monitor.

### Guides and current limits

- [Channel editing, QR sharing and camera import](docs/CHANNELS_QR.md)
- [Chat, Wi-Fi, clock and battery](docs/CHAT_CLOCK.md)
- [RAK3172H Grove wiring](docs/RAK3172_GROVE.md)
- [Launcher installation](docs/LAUNCHER.md)
- [Storage formats](docs/STORAGE_SCHEMA.md)

Channel settings are stored **on the connected radio**, not as an independent
persistent channel database on the Tab5. A readback confirms the current radio
configuration, but cannot guarantee that the radio successfully wrote its flash.
A persistence defect was reproduced in the custom RAK3172H firmware; see the
[diagnosis, patch and validation](docs/RAK_CHANNEL_PERSISTENCE.md). That patch is
for the **RAK firmware**, not the Tab5 OTA image, and still requires hardware
validation after installation.

General identity/radio configuration menus and direct-message conversations are
not implemented. Wi-Fi supports personal/open networks, not enterprise login or
captive portals. Camera interoperability, BLE/Wi-Fi coexistence and recovery
behavior should be checked on the intended hardware; the detailed guides
separate automated tests from outstanding device validation.

## Hardware setup

> **Plug the battery in first.** The Tab5 browns out (blank screen, drops off
> USB) on USB power alone — the internal battery must be connected to run or
> flash. If the screen is black after flashing, tap the power button.

The Tab5 enumerates as a USB CDC device. **Attaching serial resets the P4**, so
the on-screen diagnostics overlay (below) is the primary debugging surface, not
`idf.py monitor`.

Port assignment is **plug-order dependent**. Identify the Tab5 by its serial
before flashing (Tab5 P4 = `30:ED:A0:E1:5F:EB`):

```bash
for d in /dev/ttyACM*; do
  echo "$d -> $(udevadm info -q property -n $d | grep ID_SERIAL_SHORT | cut -d= -f2)"
done
```

## Build and install

Requires **ESP-IDF v5.4.x**. The first build downloads `esp_hosted` (which
bundles the BT-capable C6 slave firmware), `esp_wifi_remote`, and LVGL via the
component manager.

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM<Tab5> flash      # battery plugged in first!
```

For an existing launcher installation, **compile only** with `idf.py build` and
use **`build/tab5_meshtastic_v2.bin`** in the launcher's OTA/app installation flow.
Do not use a bootloader, partition-table image, or the RAK3172 firmware as the
Tab5 application image. See [the launcher guide](docs/LAUNCHER.md) for details.
Build outputs and the generated `sdkconfig` are excluded from Git; tracked
`sdkconfig.defaults` supplies the project defaults.

To start onboarding from scratch (clears saved devices **and** BLE bonds):

```bash
idf.py -p /dev/ttyACM<Tab5> erase-flash && idf.py -p /dev/ttyACM<Tab5> flash
```

## Using it

1. **Choose the radio transport:** open **RADIO**, select BLE or Grove UART,
   then restart the app. For UART, connect the RAK using the documented wiring
   and wait for synchronization. For BLE, scan, select a radio and enter its PIN
   when prompted. The saved BLE radio reconnects on later boots.
2. **Read or create channels:** open **CANAIS** after synchronization. Use
   **Reler do radio**, then create/edit a slot and **Salvar no radio**. The same
   screen offers QR sharing, camera scanning and URL import.
3. **Chat:** select the conversation in the **CHAT** header. That selection also
   determines the outgoing channel; there is no need to return to CANAIS to send.
4. **Configure the Tab5:** open **SET** for brightness, notifications, Wi-Fi,
   date/time and UTC offset. Connecting Wi-Fi triggers internet time sync;
   manual time setting remains available offline.
5. **Inspect diagnostics:** long-press the top status bar to toggle the bottom
   strip. The top bar shows the connected radio's identity and the Tab5 battery.
   **Radio sem nome** means the radio has not supplied a name.

## Architecture

Strict layering (see `PRD.md` §5) — the BLE/protocol layers never touch LVGL,
and the UI reaches the backend only through narrow commands:

```
ui/         LVGL screens, chat, channel/QR editor, camera preview and settings.
app/        Mutex-protected AppState snapshots and RTC/network clock handling.
mesh/       Pure nanopb encoding/decoding of Meshtastic messages.
protocol/   Mesh session, channel administration and channel URL handling.
transport/  Transport interface, Grove UART and serial framing.
ble/        NimBLE discovery, pairing, polling and connection recovery.
network/    Wi-Fi worker, network configuration and SNTP synchronization.
storage/    Persistent preferences and bounded message history.
board/      Display, audio, camera, battery and other Tab5 hardware integration.
```

`components/m5_tab5_component` is the in-tree board support (LCD/touch/power
expanders); `components/meshtastic_protos` is the vendored nanopb + generated
Meshtastic protobufs.

## Meshtastic BLE GATT

- Service `6ba1b218-15a8-461f-9fa8-5dcae273eafd`
- **ToRadio** (write) `f75c76d2-129e-4dad-a1dd-7866124401e7` — raw `ToRadio` protobuf
- **FromRadio** (read) `2c55e69e-4993-11ed-b878-0242ac120002` — raw `FromRadio`; 0 bytes when drained
- **FromNum** (read/notify) `ed9da18c-a800-4f66-a670-aa7547e34453` — subscribed but notifications don't traverse the hosted tunnel, so sync is **poll-driven**

Over BLE each characteristic carries a **raw protobuf** (no `0x94c3` framing —
that's serial/TCP only). Handshake: connect → bond (PIN) → MTU exchange →
discover chars → subscribe FromNum → write `want_config_id` → drain FromRadio
until `config_complete_id` → steady-state poll.
