# Build Spec

This document describes everything needed to compile the `heltec_loradmx` firmware environment from scratch: PlatformIO configuration, library manifest, build flags, required source patches, and build command reference.

---

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| Node.js | 20+ | `nvm install 20` |
| PlatformIO Core | 6.x | `pip install -r requirements.txt` |
| Python | 3.10+ | system or pyenv |

```bash
# Verify versions
node --version      # >= 20.0.0
pio --version       # PlatformIO Core >= 6.0
python3 --version   # >= 3.10

# Install dependencies
npm ci
pip install -r requirements.txt
```

---

## PlatformIO Environment

Add to `platformio_override.ini` (create if it doesn't exist). Do **not** edit `platformio.ini` directly — it is upstream and will conflict on merge.

```ini
[env:heltec_loradmx]
extends = esp32s3                          ; inherits base ESP32-S3 settings from platformio.ini
board = heltec_wifi_lora_32_V3
framework = arduino
platform = espressif32

; 8MB Flash partition table
board_upload.flash_size = 8MB
board_build.partitions = tools/WLED_ESP32_8MB.csv

; User modules to include
custom_usermods = loradmx

; C++ standard
build_unflags = -std=gnu++11
build_flags =
  ${common.build_flags}                    ; inherit WLED common flags
  ${esp32s3.build_flags}                   ; inherit ESP32-S3 flags
  ; ---- LoRa-DMX usermod ----
  -D USERMOD_LORADMX
  ; ---- Feature toggles (save ~36KB Flash) ----
  -D WLED_DISABLE_ALEXA
  -D WLED_DISABLE_HUESYNC
  -D WLED_DISABLE_INFRARED
  -D WLED_DISABLE_LOXONE
  -D WLED_DISABLE_ADALIGHT
  -D WLED_DISABLE_ESPNOW
  ; ---- DMX output (SparkFunDMX, TX on GPIO 19) ----
  -D WLED_ENABLE_DMX
  -D DMX_TX_PIN=19
  ; ---- Build identity ----
  -D WLED_RELEASE_NAME=\"LoRa-DMX\"

; Serial monitor
monitor_speed = 115200
monitor_filters = esp32_exception_decoder

; Upload (USB-Serial or OTA — change as needed)
upload_protocol = esptool
```

---

## library.json

Place at `usermods/loradmx/library.json`. This file is required for PlatformIO to resolve usermod dependencies.

```json
{
  "name": "loradmx",
  "version": "0.1.0",
  "description": "LoRaWAN Class C downlink receiver for WLED (Heltec WiFi LoRa 32 V3)",
  "keywords": ["wled", "usermod", "lorawan", "sx1262", "dmx"],
  "authors": [
    {
      "name": "Preston Bezant"
    }
  ],
  "repository": {
    "type": "git",
    "url": "https://github.com/pbezant/WLED"
  },
  "dependencies": {
    "SX126x-Arduino": "^2.0.30",
    "LoraManager2": "*"
  },
  "libArchive": false
}
```

> **`"libArchive": false`** is required to prevent PlatformIO from treating the usermod as an archive and skipping compilation of its source files.

---

## Build Flags Reference

| Flag | Value | Purpose |
|------|-------|---------|
| `USERMOD_LORADMX` | (defined) | Activates the `#ifdef` guard in `usermod_loradmx.h` and enables `REGISTER_USERMOD()` call |
| `WLED_DISABLE_ALEXA` | (defined) | Removes Alexa/Emulated Hue service |
| `WLED_DISABLE_HUESYNC` | (defined) | Removes Hue sync service |
| `WLED_DISABLE_INFRARED` | (defined) | Removes IR remote support |
| `WLED_DISABLE_LOXONE` | (defined) | Removes Loxone UDP service |
| `WLED_DISABLE_ADALIGHT` | (defined) | Removes Adalight/HyperSerial input |
| `WLED_DISABLE_ESPNOW` | (defined) | Removes ESP-NOW mesh sync |
| `WLED_ENABLE_DMX` | (defined) | Activates SparkFunDMX output path in WLED |
| `DMX_TX_PIN` | `19` | GPIO for RS485 TX (Grove connector) |
| `WLED_RELEASE_NAME` | `"LoRa-DMX"` | Shown in `/json/info` as `ver` prefix |

---

## Required Source Patches (DMX TX Pin)

WLED hardcodes GPIO 2 for DMX TX in three files. GPIO 2 is not exposed on the Heltec WiFi LoRa 32 V3 PCB headers. The following patches add a `#ifndef DMX_TX_PIN` guard so `DMX_TX_PIN=19` from build flags is used at compile time.

These changes are applied once and committed to the fork.

### Patch 1 — `wled00/src/dependencies/dmx/SparkFunDMX.cpp`

Find (around line 35):
```cpp
static const int txPin = 2;
```

Replace with:
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
static const int txPin = DMX_TX_PIN;
```

### Patch 2 — `wled00/src/dependencies/dmx/ESPDMX.cpp`

Find (around line 31):
```cpp
int sendPin = 2;
```

Replace with:
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
int sendPin = DMX_TX_PIN;
```

### Patch 3 — `wled00/wled.cpp`

Find (around line 448):
```cpp
PinManager::allocatePin(2, true, PinOwner::DMX);
```

Replace with:
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
PinManager::allocatePin(DMX_TX_PIN, true, PinOwner::DMX);
```

> After applying patches: run `npm run build` then `pio run -e heltec_loradmx` to confirm compilation succeeds.

---

## Partition Table

Uses the WLED-supplied 8MB partition file: `tools/WLED_ESP32_8MB.csv`

This provides:
- 2MB per OTA slot (double the 1.5MB default), accommodating the larger WLED + usermod binary
- 4MB LittleFS for presets, playlists, and `cfg.json`

No modifications needed to the partition file. It is referenced via `board_build.partitions` in `platformio_override.ini`.

---

## SPI Bus Allocation (Reference)

The SX1262 radio is hardwired to SPI2 (FSPI) on the Heltec V3 PCB. The usermod must instantiate its own `SPIClass` — do **not** use the default `SPI` object.

```cpp
// In usermod_loradmx.h or LoraDmxManager.cpp
#include <SPI.h>
SPIClass loraSPI(FSPI);  // FSPI = SPI2

// Before radio init:
loraSPI.begin(
  9,   // SCK
  11,  // MISO
  10,  // MOSI
  8    // NSS (CS)
);
```

| Pin | GPIO | Role |
|-----|------|------|
| SCK | 9 | SX1262 SPI clock |
| MISO | 11 | SX1262 SPI data out |
| MOSI | 10 | SX1262 SPI data in |
| NSS | 8 | SX1262 chip select |
| RST | 12 | SX1262 reset |
| BUSY | 13 | SX1262 busy signal |
| DIO1 | 14 | SX1262 IRQ |

---

## Build Commands Reference

```bash
# 1. Build web UI (required before firmware build)
npm run build

# 2. Run tests
npm test

# 3. Build LoRa-DMX firmware
pio run -e heltec_loradmx

# 4. Flash device (USB connected)
pio run -e heltec_loradmx --target upload

# 5. Monitor serial output
pio device monitor -e heltec_loradmx -b 115200

# 6. OTA update (device on WiFi)
pio run -e heltec_loradmx --target upload --upload-port <device-ip>

# 7. Clean build cache (if stale)
pio run -e heltec_loradmx --target clean

# 8. Build all default environments (CI check)
pio run
```

---

## Build Validation Checklist

Before marking a build as ready for review:

- [ ] `npm run build` completes without error
- [ ] `npm test` passes (all assertions green)
- [ ] `pio run -e heltec_loradmx` compiles without errors or warnings
- [ ] Binary size reported by PlatformIO is under 90% of OTA slot (< 1.9MB)
- [ ] `wled00/html_*.h` files are committed alongside any web UI changes
- [ ] DMX TX pin patch applied (3 files) and `DMX_TX_PIN=19` in build flags
- [ ] `usermods/loradmx/library.json` is present and valid JSON
- [ ] `platformio_override.ini` is present and contains `[env:heltec_loradmx]`

---

## Common Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `html_*.h: No such file` | Web UI not built | Run `npm run build` first |
| `USERMOD_ID_LORADMX redefined` | Conflicting ID (59 already used) | Change `USERMOD_ID_LORADMX` to unused ID |
| `SX126x.h: No such file` | Library not resolved | Check `library.json` dependencies; run `pio lib install` |
| `redefine of DMX_TX_PIN` | Macro defined twice | Ensure only one `#define DMX_TX_PIN` in the translation unit |
| `Partition table too small` | Wrong `.csv` file | Ensure `board_build.partitions = tools/WLED_ESP32_8MB.csv` |
| `undefined reference to REGISTER_USERMOD` | Header not included | Include `usermod_loradmx.h` from `usermods_list.h` (if applicable) or ensure `custom_usermods = loradmx` in env |
