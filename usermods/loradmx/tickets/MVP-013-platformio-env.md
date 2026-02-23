# MVP-013 — PlatformIO Environment

| Field | Value |
|-------|-------|
| **ID** | MVP-013 |
| **Title** | PlatformIO Environment: heltec_loradmx Compile Target |
| **Role** | Coder |
| **Status** | not-started |
| **Priority** | P0 |
| **Spec Refs** | [10-build-spec.md](../10-build-spec.md) |
| **Depends On** | MVP-002, MVP-012 |
| **Blocks** | MVP-014 |

---

## Description

Configure the `platformio_override.ini` environment for the Heltec WiFi LoRa 32 V3 target with all required build flags. This ticket produces a fully specified PlatformIO environment; MVP-014 validates it builds successfully.

---

## Scope

Create or update `platformio_override.ini` at the WLED repo root:

```ini
[env:heltec_loradmx]
extends = esp32s3
board = heltec_wifi_lora_32_V3
framework = arduino
platform = espressif32

board_upload.flash_size = 8MB
board_build.partitions = tools/WLED_ESP32_8MB.csv

custom_usermods = loradmx

build_unflags = -std=gnu++11
build_flags =
  ${common.build_flags}
  ${esp32s3.build_flags}
  -D USERMOD_LORADMX
  -D WLED_DISABLE_ALEXA
  -D WLED_DISABLE_HUESYNC
  -D WLED_DISABLE_INFRARED
  -D WLED_DISABLE_LOXONE
  -D WLED_DISABLE_ADALIGHT
  -D WLED_DISABLE_ESPNOW
  -D WLED_ENABLE_DMX
  -D DMX_TX_PIN=19
  -D WLED_RELEASE_NAME=\"LoRa-DMX\"

monitor_speed = 115200
monitor_filters = esp32_exception_decoder
upload_protocol = esptool
```

Ensure `usermods/loradmx/library.json` exists with `"libArchive": false` and correct dependencies.

---

## Acceptance Criteria

- [ ] `platformio_override.ini` exists at WLED root and is valid INI syntax
- [ ] `pio project config --json-output` lists `heltec_loradmx` as an environment
- [ ] All 11 `-D` flags are present (verify with `pio run -e heltec_loradmx -v 2>&1 | grep DMX`)
- [ ] `tools/WLED_ESP32_8MB.csv` is referenced and exists
- [ ] `platformio.ini` is not modified

---

## Out of Scope

- Actual compilation success (MVP-014)
- Upload to hardware (MVP-014 extension)
