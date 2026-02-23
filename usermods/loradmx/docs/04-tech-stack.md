# Tech Stack

## Hardware

| Component | Part | Notes |
|-----------|------|-------|
| MCU | ESP32-S3 | Dual-core, 240MHz, 512KB SRAM, 8MB PSRAM |
| LoRa Radio | SX1262 | On-board on Heltec V3, wired to SPI2 (FSPI) |
| Board | Heltec WiFi LoRa 32 V3 | `heltec_wifi_lora_32_V3` PlatformIO board ID |
| Flash | 8MB | Uses `WLED_ESP32_8MB.csv` partition table (2MB per OTA slot) |
| DMX Transceiver | Seeed Grove RS485 (103020000) | MAX485-based, UART interface, GPIO 19/20 |

---

## Firmware Base

| Component | Version | Source |
|-----------|---------|--------|
| WLED | `0.15.x` (fork) | https://github.com/pbezant/WLED |
| PlatformIO | Latest | https://platformio.org |
| ESP-IDF (via Arduino) | `5.1.x` | Managed by `platform = espressif32` |
| Arduino ESP32 | `3.x` | via PlatformIO platform |

---

## LoRaWAN Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| `SX126x-Arduino` | `^2.0.30` | Low-level SX1262 driver and LoRaWAN stack |
| `LoraManager2` | `latest` (pbezant/LoraManager2) | Higher-level LoRaWAN session management, wraps SX126x-Arduino |

Both are declared as dependencies in the usermod's `library.json`.

**ArduinoJson conflict note:** WLED bundles its own ArduinoJson version. If `SX126x-Arduino` pulls in a conflicting version, the usermod must explicitly exclude it in `library.json` and use WLED's bundled version.

---

## WLED Built-in Features Used

| Feature | Flag | Purpose |
|---------|------|---------|
| DMX serial output | `WLED_ENABLE_DMX` | Drives DMX fixtures via SparkFunDMX over UART2 |
| Preset system | Built-in | `applyPreset(N)` called by WLEDMapper |
| JSON API | Built-in | `/json/state`, `/json/info`, `/json/cfg` |
| PinManager | Built-in | Allocate SPI/GPIO pins without conflicts |
| `cfg.json` config | Built-in | Usermod settings persisted via `addToConfig` |
| OTA | Built-in | Firmware updates via WLED `/update` endpoint |

---

## WLED Features Disabled (for Flash budget)

| Feature | Flag | Flash saved |
|---------|------|-------------|
| Alexa | `WLED_DISABLE_ALEXA` | ~11KB |
| Hue Sync | `WLED_DISABLE_HUESYNC` | ~4KB |
| Infrared | `WLED_DISABLE_INFRARED` | ~12KB |
| Loxone | `WLED_DISABLE_LOXONE` | ~1.2KB |
| Adalight | `WLED_DISABLE_ADALIGHT` | ~5KB |
| ESP-NOW | `WLED_DISABLE_ESPNOW` | ~3KB |
| **Total savings** | | **~36KB+** |

---

## Build Toolchain

| Tool | Purpose |
|------|---------|
| PlatformIO Core | Build, dependency management, upload |
| `load_usermods.py` | Auto-discovers `usermods/loradmx/` and injects as `symlink://` lib dep |
| `validate_modules.py` | Validates `library.json` has `libArchive: false` |
| `build_ui.py` | Regenerates WLED web UI headers (must run before hardware build) |
| `npm run build` | Executes `build_ui.py` via Node wrapper |
| `clang-format` | C++ formatting (not enforced in CI, follow project style) |

---

## LoRaWAN Network Servers (Supported)

| LNS | Compatibility |
|-----|--------------|
| The Things Network (TTN) v3 | Supported; `appEUI` = all-zeros is valid |
| ChirpStack v4 | Supported; configure application and device profile for OTAA |
| Helium Network | Supported via Helium Console or LNS API |
| Any OTAA-compatible LNS | Supported if OTAA + US915 subband 2 + Class C is available |

---

## Development Environment

| Requirement | Version |
|-------------|---------|
| Node.js | 20+ (see `.nvmrc`) |
| Python | 3.x |
| PlatformIO | Latest via `pip install -r requirements.txt` |
| VS Code | Recommended with PlatformIO extension |
| macOS / Linux | Supported. Windows: use WSL2 for best results |
