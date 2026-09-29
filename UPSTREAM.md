# Upstream Attribution

This project is derived from:

- **Repository**: https://github.com/hardparking/Tab5-Meshtastic
- **Author**: hardparking (GitHub)
- **Last commit used as base**: 2075778 ("Let the physical keyboard type into chat without opening the OSK first")

## License Status

The upstream repository does **not** contain an explicit license file.
Without a license, default copyright law applies: the original author retains
all rights, and no permission to reproduce, distribute, or create derivative
works is granted.

This fork has therefore been kept **local** and is **not published** or
redistributed. It exists only for private development and testing.

## What this fork adds

- UART transport for RAK3172H via Grove port (GPIO53/GPIO54, 115200 baud)
- Transport abstraction layer (IMeshTransport interface)
- MeshSession: reusable Meshtastic protocol handling above both transports
- SerialFramer: non-blocking 0x94C3 frame parser
- Persistent message history (SPIFFS, versioned binary format)
- Persistent app settings (transport selection, channel, UI preferences)
- Channel management (list, create, edit, PSK, primary/secondary/disabled)
- Screen brightness control
- Audio notification settings (ES8388 via BSP)
- M5Launcher OTA compatibility and backup/restore documentation

All original code and commit history from upstream is preserved.
