# WLED Cloud Usermod

Connects a WLED ESP32 device to [WLED Cloud](https://github.com/prestonbezant/WLED-Cloud) for remote monitoring and control.

## Features

- **Device claiming** — secure zero-credential pairing via a 7-character claim code
- **Real-time state sync** — instant push on every WLED state change
- **Remote command execution** — full `/json/state` API support (color, effects, presets, segments)
- **Telemetry** — RSSI, heap, and uptime reported periodically
- **Remote OTA** — firmware updates triggered from the cloud dashboard
- **Exponential reconnect backoff** — 1 s → 30 s with ±25 % jitter

## Requirements

- ESP32 (ESP8266 not supported — no `beginSSL`)
- WLED firmware with PlatformIO build
- A running [WLED Cloud](https://github.com/prestonbezant/WLED-Cloud) instance

## Build

1. Ensure `usermods/wledcloud/` is present in your WLED checkout.
2. Add to `platformio_override.ini` (or use the provided snippet at the bottom of the root `platformio_override.ini`):

```ini
[env:esp32dev_wledcloud]
extends = env:esp32dev
custom_usermods = wledcloud
build_flags =
  ${env:esp32dev.build_flags}
  -D USERMOD_WLEDCLOUD
```

3. Build:

```bash
pio run -e esp32dev_wledcloud
```

## Configuration Fields

Set via **WLED Settings → Usermods → WLEDCloud**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Master enable switch |
| `server` | string | `cloud.wled.me` | Cloud server hostname (no `https://` prefix) |
| `port` | uint16 | `443` | Server port |
| `tls` | bool | `true` | Use WSS/HTTPS (set false for local/self-signed) |
| `syncSec` | uint16 | `30` | Seconds between periodic state syncs (10–300) |
| `telemetry` | bool | `true` | Send RSSI/heap/uptime telemetry |
| `token` | string | `""` | Device token — **set automatically by claim flow** |
| `venueId` | string | `""` | Venue UUID — **set automatically by claim flow** |

> **token** and **venueId** are written by the claim flow. Do not set manually unless migrating a device.

## Claim Flow

1. Enable the usermod and set `server` / `port`.
2. A 7-character claim code (e.g. `X7K-9P2`) appears in **WLED Info → Cloud**.
3. In the WLED Cloud dashboard, go to **Devices → Add Device** and enter the code.
4. The device receives its token, saves it to `cfg.json`, and connects automatically.
5. The claim code disappears from the Info panel once connected.

If not claimed within 5 minutes the code expires and a new one is generated.

## Info Panel

The usermod adds a **Cloud** row to the WLED Info page (`/json/info`):

| State | Display |
|-------|---------|
| Disabled | `Disabled` |
| Awaiting claim | `X7K-9P2 (enter in dashboard)` |
| Connected | `Connected` |
| Token set, reconnecting | `Reconnecting...` |
| No token | `Not claimed` |

## Dependencies

| Library | Version | Source |
|---------|---------|--------|
| WebSockets | ^2.4.0 | links2004/WebSockets |
| ArduinoJson | ^7.0.0 | bblanchon/ArduinoJson (bundled with WLED) |
| HTTPClient | built-in | ESP32 Arduino core |
