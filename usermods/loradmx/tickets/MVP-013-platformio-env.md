# MVP-013 — PlatformIO Environment

| Field | Value |
|-------|-------|
| **ID** | MVP-013 |
| **Title** | PlatformIO Environment: heltec_loradmx Compile Target |
| **Role** | Coder |
| **Status** | completed |
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

- [x] `platformio_override.ini` exists at WLED root and is valid INI syntax
- [x] `pio project config --json-output` lists `heltec_loradmx` as an environment
- [x] All `-D` flags are present (`USERMOD_LORADMX`, `WLED_ENABLE_DMX`, `DMX_TX_PIN=19`, all 6 `WLED_DISABLE_*`, `WLED_RELEASE_NAME`)
- [x] `tools/WLED_ESP32_8MB.csv` is referenced and exists
- [x] `platformio.ini` is not modified

---

## Implementation Notes

**Completed:** 2026-02-22

Final env config uses explicit `platform`/`platform_packages`/`lib_deps` references (not `extends = esp32s3`) — the `extends` shorthand caused `sdkconfig.h` not found errors because PlatformIO couldn't resolve the inherited package list.

Additional board settings required for Tasmota ESP-IDF 4.4 framework:
- `board_build.arduino.memory_type = qio_qspi` — selects the no-PSRAM sdkconfig variant
- `board_build.flash_mode = qio` — Tasmota framework only ships QIO bootloaders for ESP32S3
- `board_build.f_flash = 80000000L` — 80 MHz flash clock
- `ARDUINO_USB_CDC_ON_BOOT=0`, `ARDUINO_USB_MODE=0` — Heltec V3 uses CH340 UART chip, not USB-OTG

Spec template in this ticket used `extends = esp32s3` which does not work; spec [10-build-spec.md](../docs/10-build-spec.md) should be updated to reflect the working pattern.

---

## Out of Scope

- Actual compilation success (MVP-014)
- Upload to hardware (MVP-014 extension)
